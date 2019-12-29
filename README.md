# DISCLAIMER

Pebble Authenticator (a fork of [pTOTP](https://github.com/abl/pTOTP)) is meant to be used **with another official & reliable authenticator**. Do not make it the sole means of accessing your accounts.

While I have done my best to guard the credentials you enter in this application, I cannot guarantee that they will remain forever safe from prying eyes.

# Build it yourself
Nothing more than the standard [Pebble 2.0 SDK](https://developer.getpebble.com/2/getting-started/) is required to build and run this app. You may wish to change the configuration page URL in the `showConfiguration` event handler to point at a local development server.

# Features
Forked from https://github.com/cpfair/pTOTP 
* Google Authenticator compatible verification codes
* SHA256 for AWS 64 character keys

This repo adds:
* Specify code length (default 6, supports Battle.net codes by specifying 8)
* Adds a space in the middle of the code when using more than 6 digits
