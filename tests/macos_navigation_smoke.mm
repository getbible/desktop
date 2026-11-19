// Run the shell's configured WKWebView against two loopback HTTPS origins.
#define main desktop_application_main
#include "../src/main.cpp"
#undef main

#import <Cocoa/Cocoa.h>
#import <Security/Security.h>
#import <WebKit/WebKit.h>

#include <stdexcept>

// Observe the application's decisions so the draft is checked after WebKit
// has processed the redirect. Other navigation callbacks go to CHOC as usual.
@interface FixtureDelegate : NSObject <WKNavigationDelegate>
@property(assign) id<WKNavigationDelegate> appDelegate;
@property(retain) NSData* fixtureCertificate;
@property(copy) NSURL* rejectedURL;
@end

@implementation FixtureDelegate
- (BOOL)respondsToSelector:(SEL)selector
{
    return [super respondsToSelector:selector] || [self.appDelegate respondsToSelector:selector];
}

- (id)forwardingTargetForSelector:(SEL)selector
{
    return [self.appDelegate respondsToSelector:selector] ? self.appDelegate
                                                          : [super forwardingTargetForSelector:selector];
}

- (void)webView:(WKWebView*)view decidePolicyForNavigationAction:(WKNavigationAction*)action
    decisionHandler:(void (^)(WKNavigationActionPolicy))decide
{
    if (![self.appDelegate respondsToSelector:_cmd]) { decide(WKNavigationActionPolicyAllow); return; }
    [self.appDelegate webView:view decidePolicyForNavigationAction:action decisionHandler:^(WKNavigationActionPolicy policy) {
        if (policy == WKNavigationActionPolicyCancel) self.rejectedURL = action.request.URL;
        decide(policy);
    }];
}

- (void)webView:(WKWebView*)view decidePolicyForNavigationResponse:(WKNavigationResponse*)response
    decisionHandler:(void (^)(WKNavigationResponsePolicy))decide
{
    if (![self.appDelegate respondsToSelector:_cmd]) { decide(WKNavigationResponsePolicyAllow); return; }
    [self.appDelegate webView:view decidePolicyForNavigationResponse:response decisionHandler:^(WKNavigationResponsePolicy policy) {
        if (policy == WKNavigationResponsePolicyCancel) self.rejectedURL = response.response.URL;
        decide(policy);
    }];
}

- (void)webView:(WKWebView*)view didReceiveAuthenticationChallenge:(NSURLAuthenticationChallenge*)challenge
    completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential*))reply
{
    NSURLProtectionSpace* space = challenge.protectionSpace;
    if ([space.authenticationMethod isEqual:NSURLAuthenticationMethodServerTrust] &&
        [space.host isEqualToString:@"127.0.0.1"] && space.serverTrust) {
        SecCertificateRef leaf = SecTrustGetCertificateAtIndex(space.serverTrust, 0);
        if (leaf) {
            CFDataRef bytes = SecCertificateCopyData(leaf);
            BOOL matches = bytes && [self.fixtureCertificate isEqualToData:(NSData*)bytes];
            if (bytes) CFRelease(bytes);
            if (matches) {
                reply(NSURLSessionAuthChallengeUseCredential, [NSURLCredential credentialForTrust:space.serverTrust]);
                return;
            }
        }
    }
    reply(NSURLSessionAuthChallengePerformDefaultHandling, nil);
}
@end

namespace
{
void waitFor(const char* message, bool (^ready)())
{
    NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:15];
    while (!ready()) {
        if (deadline.timeIntervalSinceNow <= 0)
            throw std::runtime_error(message);
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
}

void requireScript(WKWebView* view, NSString* script, const char* message)
{
    __block BOOL complete = NO;
    __block BOOL passed = NO;
    [view evaluateJavaScript:script completionHandler:^(id value, NSError* error) {
        passed = !error && [@YES isEqual:value];
        if (error) NSLog(@"JavaScript failed: %@", error.localizedDescription);
        complete = YES;
    }];
    waitFor("JavaScript evaluation timed out", ^{ return complete != NO; });
    if (!passed) throw std::runtime_error(message);
}

void runTest(NSString* allowedOrigin, NSString* blockedOrigin, NSString* certificate)
{
    [NSApplication sharedApplication];
    [NSApp finishLaunching];
    choc::messageloop::initialise();
    choc::ui::WebView::Options options;
    options.webviewIsReady = configureWebView;
    choc::ui::WebView app(options);
    if (!app.loadedOK()) throw std::runtime_error("Unable to create WKWebView");
    WKWebView* view = (WKWebView*)app.getViewHandle();
    FixtureDelegate* observer = [[[FixtureDelegate alloc] init] autorelease];
    observer.appDelegate = view.navigationDelegate;
    observer.fixtureCertificate = [NSData dataWithContentsOfFile:certificate];
    view.navigationDelegate = observer;

    choc::ui::DesktopWindow window({0, 0, 600, 400});
    window.setContent(app.getViewHandle());
    window.setVisible(true);
    NSURL* home = [NSURL URLWithString:[allowedOrigin stringByAppendingString:@"/"]];
    [view loadRequest:[NSURLRequest requestWithURL:home]];
    waitFor("Initial fixture page did not load", ^{ return [view.URL isEqual:home] && !view.loading; });
    requireScript(view, @"(() => { document.querySelector('#unsaved').value = 'unsaved draft'; "
                         "window.originalDocument = true; return true; })()",
                  "Could not seed the original document");

    NSURL* blocked = [NSURL URLWithString:[blockedOrigin stringByAppendingString:@"/destination"]];
    for (NSURL* start in @[blocked, [NSURL URLWithString:@"/redirect-blocked" relativeToURL:home].absoluteURL]) {
        observer.rejectedURL = nil;
        [view loadRequest:[NSURLRequest requestWithURL:start]];
        waitFor("The app did not cancel the blocked destination", ^{
            return [observer.rejectedURL isEqual:blocked] && !view.loading;
        });
        if (![view.URL isEqual:home]) throw std::runtime_error("Blocked navigation replaced the original URL");
        requireScript(view, @"window.originalDocument === true && document.querySelector('#unsaved')?.value === 'unsaved draft'",
                      "Blocked navigation discarded the original document or draft");
    }

    NSURL* destination = [NSURL URLWithString:@"/destination" relativeToURL:home].absoluteURL;
    [view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:@"/redirect-allowed" relativeToURL:home].absoluteURL]];
    waitFor("Allowed redirect did not finish", ^{ return [view.URL isEqual:destination] && !view.loading; });
    requireScript(view, @"!!document.querySelector('#destination') && window.originalDocument === undefined",
                  "Allowed redirect did not display its destination");
    view.navigationDelegate = observer.appDelegate;
    observer.fixtureCertificate = nil;
    observer.rejectedURL = nil;
}
}

int main(int argc, char** argv)
{
    if (argc != 4) return 2;
    @autoreleasepool {
        try {
            runTest([NSString stringWithUTF8String:argv[1]], [NSString stringWithUTF8String:argv[2]],
                    [NSString stringWithUTF8String:argv[3]]);
        } catch (const std::exception& error) {
            std::cerr << "WKWebView navigation smoke: " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "WKWebView retained the unsaved draft on direct and redirected rejection and followed the allowed redirect.\n";
    return 0;
}
