var Tokens = [];
var BlockedIDs = [];
var SelectedTokenID;
var PendingWatchUpdate = false;

var TokenByID = function(id) {
    for (var idx in Tokens) {
        if (Tokens[idx].ID == id) return Tokens[idx];
    }
};

var NextTokenID = function(){
    for (var id = 0; id < 256; id++) {
        if (!TokenByID(id) && BlockedIDs.indexOf(id) < 0) return id;
    }
};

$(document).ready(function(e) {
    if (location.hash) {
        try {
            Tokens = JSON.parse(decodeURIComponent(location.hash.substring(1)));
        } catch (exc) {

        }
    }

    // This is pretty terrible...
    if (!window.location.href.match(/ver=1\.1\.0/)) {
        alert("You're running an old verison of Pebble Authenticator - please update!");
        ConfigurationSave();
        return;
    }

    $('.token-list-container').sortable({
            'tolerance': 'pointer',
            'containment': 'parent',
            'opacity': 0.6,
            update: function(event, ui) {
                // Refresh the Tokens list to match
                var newTokens = [];
                $(".token-list-container a").each(function(){
                    newTokens.push(TokenByID($(this).data("id")));
                });
                if (Tokens != newTokens) {
                    Tokens = newTokens;
                    RefreshTokenList();
                    SetPendingWatchUpdate();
                }
            }
        });

    $("#token-detail").on("pagebeforeshow", function(){
        $("input#token-name").val(TokenByID(SelectedTokenID).Name);
    });

    $("#token-new").on("pagebeforeshow", function(){
        $("#token-new input[type='text']").val("");
    });

    $("#config-save-btn").bind("click", ConfigurationSave).hide();

    $("#token-create-btn").bind("click", TokenCreated);

    $("#new-token-key").bind("keyup", function(e){
        if (e.keyCode == 13) {
            TokenCreated();
        }
    });

    $("#token-save-btn").bind("click", TokenSaved);
    $("#token-delete-btn").bind("click", TokenDeleted);

    $("#token-name").bind("keyup", function(e){
        if (e.keyCode == 13) {
            TokenSaved();
        }
    });

    RefreshTokenList();
});

var SetPendingWatchUpdate = function(){
    $("#config-save-btn").show();
};

var RefreshTokenList = function(){
    $(".token-list-container").empty();
    var has_tokens = false;
    for (var idx in Tokens) {
        var token_item = $("<li><a href=\"#token-detail\" class=\"ui-icon-gear\"></a></li>");
        $("a", token_item).data("id", Tokens[idx].ID);
        $("a", token_item).text(Tokens[idx]["Name"]).bind("click", TokenSelected);
        $(".token-list-container").append(token_item).listview("refresh");
        has_tokens = true;
    }
    $("#no-tokens-message").toggle(!has_tokens);
};

var TokenSelected = function(){
    SelectedTokenID = $(this).data("id");
};

var TokenSaved = function(){
    if (!$("#token-name").val()) {
        alert("You must enter a name for this token");
        return;
    }
    TokenByID(SelectedTokenID).Name = $("#token-name").val();
    RefreshTokenList();
    SetPendingWatchUpdate();
    window.location.hash = "";
};

var TokenDeleted = function(){
    if (!confirm("Are you sure?")) return;
    BlockedIDs.push(SelectedTokenID); // So we don't re-use it for a new token immediately.
    Tokens.splice(Tokens.indexOf(TokenByID(SelectedTokenID)), 1);
    RefreshTokenList();
    SetPendingWatchUpdate();
    window.location.hash = "";
};

var TokenCreated = function(e){
    var raw_secret;
    try {
        raw_secret = base32_decode($("#new-token-key").val());
    } catch (exc) {
        alert("The key could not be decoded - make sure it's typed correctly.");
        return;
    }
    var base64_secret = btoa(raw_secret); // Annnd right back to base64 - so I don't need to write a JS base32 encoder too.
    var token = {
        "ID": NextTokenID(),
        "Name": $("#new-token-name").val(),
        "Secret": base64_secret,
        "Digits": parseInt($("#new-token-digits").val())
    };
    if (!token.Name || !token.Secret) {
        alert("You must enter a name and key for the new token");
        return;
    }
    if (isNaN(token.Digits) || token.Digits < 1 || token.Digits > 10) {
        token.Digits = 6;
    }
    Tokens.push(token);
    SetPendingWatchUpdate();
    RefreshTokenList();
    window.location.hash = "";
};

var ConfigurationSave = function(){
    window.location.href = "pebblejs://close#" + encodeURIComponent(JSON.stringify(Tokens));
};

// base32 stuff - reimplemented from the C equivalent.
var base32_decode = function(input) {
    var alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567=";
    var buffer = 0;
    var bitsLeft = 0;
    var result = "";
    var i = 0;
    var count = 0;
    input = input.toUpperCase();
    while (i < input.length) {
        var ch = input.charAt(i++);
        if (ch == ' ') continue;
        if (ch == '0') {
            ch = 'O';
        } else if (ch == '1') {
            ch = 'L';
        } else if (ch == '8') {
            ch = 'B';
        }
        var val = alphabet.indexOf(ch);
        if (val >= 0 && val < 32) {
            buffer <<= 5;
            buffer |= val;
            bitsLeft += 5;
            if (bitsLeft >= 8) {
                result += String.fromCharCode((buffer >> (bitsLeft - 8)) & 0xFF);
                bitsLeft -= 8;
            }
        } else {
            throw Error("Character " + ch + " out of range");
        }
    }
    return result;
};
