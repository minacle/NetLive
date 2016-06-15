#import "AppDelegate.h"
#include "netlive.h"

#pragma clang diagnostic ignored "-Wlogical-op-parentheses"

NSString *statusStringFromState(BOOL available, BOOL reachable, BOOL timeout) {
    return available ? reachable ? @"Reachable" : timeout ? @"Timeout" : @"Unreachable" : @"Unavailable";
}

@interface AppDelegate () <NSMenuDelegate>

@property IBOutlet NSMenu *mainMenu;
@property IBOutlet NSMenuItem *descriptionMenuItem;
@property IBOutlet NSMenuItem *detailMenuSeparator;
@property IBOutlet NSMenuItem *routerMenuItem;
@property IBOutlet NSMenuItem *IPv4MenuItem;
@property IBOutlet NSMenuItem *IPv6MenuItem;
@property IBOutlet NSMenuItem *domainMenuItem;
@property IBOutlet NSMenuItem *refreshMenuSeparator;
@property IBOutlet NSMenuItem *refreshIntervalMenuItem;
@property IBOutlet NSMenuItem *refreshTimeoutMenuItem;

@property NSStatusItem *statusItem;

@property NSImage *offlineIcon;
@property NSImage *unstableIcon;
@property NSImage *stableIcon;

@property NSTimer *refreshTimer;

@property NSTimeInterval refreshInterval;
@property NSTimeInterval refreshTimeout;

- (void)refreshWithState:(uint16_t)state;

@end

void netlive_handler(uint16_t state) {
    [(AppDelegate *)[[NSApplication sharedApplication] delegate] refreshWithState:state];
}

@implementation AppDelegate

{
    NSTimeInterval refreshInterval;
    NSTimeInterval refreshTimeout;
    uint16_t lastState;
}

- (NSTimeInterval)refreshInterval {
    return refreshInterval;
}

- (void)setRefreshInterval:(NSTimeInterval)_refreshInterval {
    refreshInterval = _refreshInterval;
    for (NSMenuItem *item in [[[self refreshIntervalMenuItem] submenu] itemArray]) {
        if ([item tag] == refreshInterval)
            [item setState:NSOnState];
        else
            [item setState:NSOffState];
    }
    [self resetTimer];
}

- (NSTimeInterval)refreshTimeout {
    return refreshTimeout;
}

- (void)setRefreshTimeout:(NSTimeInterval)_refreshTimeout {
    refreshTimeout = _refreshTimeout;
    for (NSMenuItem *item in [[[self refreshTimeoutMenuItem] submenu] itemArray]) {
        if ([item tag] == refreshTimeout * 1000)
            [item setState:NSOnState];
        else
            [item setState:NSOffState];
    }
}

- (void)applicationDidFinishLaunching:(NSNotification *)aNotification {
    NSStatusItem *statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
    [self setOfflineIcon:[NSImage imageNamed:@"offline"]];
    [self setUnstableIcon:[NSImage imageNamed:@"unstable"]];
    [self setStableIcon:[NSImage imageNamed:@"stable"]];
    [[self offlineIcon] setTemplate:YES];
    [[self unstableIcon] setTemplate:YES];
    [[self stableIcon] setTemplate:YES];
    [[self mainMenu] setDelegate:self];
    [statusItem setMenu:[self mainMenu]];
    [self setStatusItem:statusItem];
    [self setRefreshTimeout:0.5];
    [self setRefreshInterval:5];
    [self refresh:nil];
}

- (void)applicationWillTerminate:(NSNotification *)aNotification {
    if ([[self refreshTimer] isValid])
        [[self refreshTimer] invalidate];
}

- (void)menuWillOpen:(NSMenu *)menu {
    if ([NSEvent modifierFlags] & NSAlternateKeyMask) {
        [[self detailMenuSeparator] setHidden:NO];
        [[self routerMenuItem] setHidden:NO];
        [[self IPv4MenuItem] setHidden:NO];
        [[self IPv6MenuItem] setHidden:NO];
        [[self domainMenuItem] setHidden:NO];
        [[self refreshMenuSeparator] setHidden:NO];
        [[self refreshIntervalMenuItem] setHidden:NO];
        [[self refreshTimeoutMenuItem] setHidden:NO];
    }
    else {
        if (lastState & NETLIVE_TIMEOUT || ~lastState & NETLIVE_ROUTER_AVAILABLE || ~lastState & NETLIVE_ROUTER_REACHABLE) {
            [[self detailMenuSeparator] setHidden:NO];
            [[self routerMenuItem] setHidden:NO];
        }
        else if (~lastState & NETLIVE_IPV4_AVAILABLE && ~lastState & NETLIVE_IPV6_AVAILABLE || ~lastState & NETLIVE_IPV4_REACHABLE && ~lastState & NETLIVE_IPV6_REACHABLE) {
            [[self detailMenuSeparator] setHidden:NO];
            [[self IPv4MenuItem] setHidden:NO];
            [[self IPv6MenuItem] setHidden:NO];
        }
        else if (lastState & NETLIVE_IPV4_AVAILABLE && ~lastState & NETLIVE_IPV4_REACHABLE) {
            [[self detailMenuSeparator] setHidden:NO];
            [[self IPv4MenuItem] setHidden:NO];
        }
        else if (lastState & NETLIVE_IPV6_AVAILABLE && ~lastState & NETLIVE_IPV6_REACHABLE) {
            [[self detailMenuSeparator] setHidden:NO];
            [[self IPv6MenuItem] setHidden:NO];
        }
    }
}

- (void)menuDidClose:(NSMenu *)menu {
    [[self detailMenuSeparator] setHidden:YES];
    [[self routerMenuItem] setHidden:YES];
    [[self IPv4MenuItem] setHidden:YES];
    [[self IPv6MenuItem] setHidden:YES];
    [[self domainMenuItem] setHidden:YES];
    [[self refreshMenuSeparator] setHidden:YES];
    [[self refreshIntervalMenuItem] setHidden:YES];
    [[self refreshTimeoutMenuItem] setHidden:YES];
}

- (void)setIconWithState:(uint16_t)state {
    [[self routerMenuItem] setTitle:[NSString stringWithFormat:@"Router: %@", statusStringFromState(state & NETLIVE_ROUTER_AVAILABLE, state & NETLIVE_ROUTER_REACHABLE, state & NETLIVE_TIMEOUT)]];
    [[self IPv4MenuItem] setTitle:[NSString stringWithFormat:@"IPv4: %@", statusStringFromState(state & NETLIVE_IPV4_AVAILABLE, state & NETLIVE_IPV4_REACHABLE, state & NETLIVE_TIMEOUT)]];
    [[self IPv6MenuItem] setTitle:[NSString stringWithFormat:@"IPv6: %@", statusStringFromState(state & NETLIVE_IPV6_AVAILABLE, state & NETLIVE_IPV6_REACHABLE, state & NETLIVE_TIMEOUT)]];
    [[self domainMenuItem] setTitle:[NSString stringWithFormat:@"Domain: %@", statusStringFromState(YES, state & NETLIVE_DOMAIN_REACHABLE, state & NETLIVE_TIMEOUT)]];
    NSButton *statusButton = [[self statusItem] button];
    NSMenuItem *description = [self descriptionMenuItem];
    if (~state & NETLIVE_TIMEOUT && state & NETLIVE_DOMAIN_REACHABLE) {
        [description setTitle:@"Stable"];
        [statusButton setImage:[self stableIcon]];
    }
    else if (state & (NETLIVE_IPV4_REACHABLE | NETLIVE_IPV6_REACHABLE)) {
        [description setTitle:@"Unstable"];
        [statusButton setImage:[self unstableIcon]];
    }
    else {
        [description setTitle:@"Offline"];
        [statusButton setImage:[self offlineIcon]];
    }
    lastState = state;
}

- (void)resetTimer {
    if ([self refreshTimer] && [[self refreshTimer] isValid])
        [[self refreshTimer] invalidate];
    [self setRefreshTimer:[NSTimer scheduledTimerWithTimeInterval:[self refreshInterval] target:self selector:@selector(refresh:) userInfo:nil repeats:YES]];
}

- (void)refresh:(id)sender {
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_BACKGROUND, 0), ^{
            netlive_once();
        });
        [NSThread sleepForTimeInterval:[self refreshTimeout]];
        netlive_cancel();
    });
}

- (void)refreshWithState:(uint16_t)state {
    if (state & NETLIVE_WAITING)
        return;
    [self setIconWithState:state];
}

- (IBAction)refreshInterval:(id)sender {
    [self setRefreshInterval:[sender tag]];
}

- (IBAction)refreshTimeout:(id)sender {
    [self setRefreshTimeout:[sender tag] / 1000.0];
}

@end
