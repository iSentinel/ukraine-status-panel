# Web page language

Run `node tests/web-page-language-browser.cjs --help` for usage.
The test runs the actual readable or embedded UI with local source events.
It checks en, ru and uk, keeps the five existing field labels associated
with their controls, and changes language on an already open page.

No physical panel or setting is changed by these fixtures.
The HTML language attribute is synchronized when the header renders.
The visible text, layout and actions stay the same. This check does not
claim complete accessibility standards compliance.
