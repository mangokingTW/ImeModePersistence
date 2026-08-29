// Google Ads outbound-click conversion for the Microsoft Store links.
//
// Shared by the landing page and every generated wiki page (see
// tools/wiki-pages/build.mjs), so the behaviour lives in one file rather than
// drifting between copies. The gtag base tag stays inline in each page's head:
// that is Google's canonical snippet and it has to define window.dataLayer
// before the async loader runs, which a deferred file cannot promise.
//
// Loading this file is optional in the sense that matters: if it never
// arrives, the Store links are ordinary links and simply go to the Store
// uncounted.

function gtag_report_conversion(url) {
  var navigated = false;
  var go = function () {
    if (navigated) { return; }        // event_callback and the timer can both fire
    navigated = true;
    if (typeof url !== 'undefined') { window.location = url; }
  };

  // Reporting a click must never cost someone the download, so every path out
  // of here still navigates: a blocked or missing gtag throws and goes
  // immediately, event_timeout bounds the tag's own wait, and the timer covers
  // a callback that never arrives at all.
  try {
    gtag('event', 'conversion', {
      'send_to': 'AW-18414547014/FpC1CKKx--kcEMbg3sxE',
      'event_callback': go,
      'event_timeout': 1000
    });
  } catch (e) {
    go();
    return false;
  }
  setTimeout(go, 1200);
  return false;
}

// Bound by delegation rather than an inline onclick per link: the landing page
// alone has four Store links (the badge and one in each language's install
// paragraph), and this way a link added later is counted without anyone having
// to remember.
document.addEventListener('click', function (e) {
  var link = e.target && e.target.closest && e.target.closest('a[href*="apps.microsoft.com"]');
  if (!link) { return; }
  // Leave modified and non-primary clicks alone -- those open a new tab, and
  // rewriting window.location would hijack the tab the reader is still on.
  if (e.defaultPrevented || e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) { return; }
  e.preventDefault();
  gtag_report_conversion(link.href);
});
