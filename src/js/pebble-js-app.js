var Tokens = [];
var TokenLoadFinished = false;

Pebble.addEventListener("ready",
    function(e) {
        QueueAppMessage({ "AMReadTokenList": 1});
        QueueAppMessage({ "AMSetUTCOffset": (new Date()).getTimezoneOffset() * -60});
    }
);

var UnCString = function(array, offset) {
    var string = "";
    for (var i = offset; i < array.length; i++){
        if (array[i] === 0) break;
        string += String.fromCharCode(array[i]);
    }
    return string;
};

var ToByteArray = function(input) {
    // Otherwise the JS appmessage framework will mangle these characters on transmission.
    return input.split('').map(function(e){return e.charCodeAt(0);});
};

var TokenByID = function(id) {
    for (var idx in Tokens) {
        if (Tokens[idx].ID == id) return Tokens[idx];
    }
};

var AMQueue = [];
var AMActiveMessage;
var AMPending = false;
var QueueAppMessage = function(message) {
    AMQueue.push(message);
    if (!AMPending) AMTransmitQueue();
};

var AMQueueFail = function(txn) {
    console.log("AM transmit failed: " + JSON.stringify(txn) + ", retrying");
    AMQueue.unshift(AMActiveMessage);
    AMTransmitQueue();
};

var AMTransmitQueue = function() {
    if (!AMQueue.length) {
        AMPending = false;
        return;
    }
    AMPending = true;
    var message = AMQueue.shift();
    AMActiveMessage = message;
    Pebble.sendAppMessage(message, AMTransmitQueue, AMQueueFail);
};

Pebble.addEventListener("appmessage",
  function(e) {
    if (e.payload.AMReadTokenList_Result) {
        var token = {};
        token.ID = e.payload.AMReadTokenList_Result[0];
        token.Name = UnCString(e.payload.AMReadTokenList_Result, 2);
        Tokens.push(token);
    }
    if (e.payload.AMReadTokenList_Finished) {
        TokenLoadFinished = true;
    }
  }
);

var OpenConfiguration = function(){
    // You used to be able to defer opening the config page till the tokens were loaded.
    // You can't any more...
    if (TokenLoadFinished) {
        Pebble.openURL("https://pebbleauth.cpfx.ca/config.html?ver=1.12#" + encodeURIComponent(JSON.stringify(Tokens)));
    } else {
        Pebble.openURL("https://pebbleauth.cpfx.ca/relaunch.html");
    }
};

Pebble.addEventListener("showConfiguration", OpenConfiguration);

var ReconcileConfiguration = function(newTokens) {
    var existing_ids = [];
    var new_ids = [];
    var idx, token; // So JSLint stops complaining.
    for (idx in Tokens) { existing_ids.push(Tokens[idx].ID); } // I can pretend it's list comprehension, right?
    for (idx in newTokens) { new_ids.push(newTokens[idx].ID); }
    // Which have been deleted (by ID only)?
    var to_delete_ids = [];
    for(idx in existing_ids) {
        if (new_ids.indexOf(existing_ids[idx]) < 0) to_delete_ids.push(existing_ids[idx]);
    }

    // Which have been added?
    // Which have been modified?
    var to_create = [];
    var to_update = [];
    for(idx in newTokens) {
        if (existing_ids.indexOf(newTokens[idx].ID) < 0) {
            to_create.push(newTokens[idx]);
        } else {
            if (TokenByID(newTokens[idx].ID).Name != newTokens[idx].Name) {
                to_update.push(newTokens[idx]);
            }
        }
    }

    console.log("Creating " + JSON.stringify(to_create.map(function(e){return {"Name": e.Name, "ID": e.ID};}))); // Strip the secret so it doesn't appear in logs.
    console.log("Updating " + JSON.stringify(to_update));
    console.log("Deleting " + JSON.stringify(to_delete_ids));

    // Apply updates
    for(idx in to_delete_ids) {
        QueueAppMessage({"AMDeleteToken": [to_delete_ids[idx]]});
    }
    for (idx in to_create) {
        token = to_create[idx];
		var secretArray = ToByteArray(atob(token.Secret));
		secretArray.unshift(secretArray.length);
        QueueAppMessage({"AMCreateToken": secretArray, "AMCreateToken_ID": token.ID, "AMCreateToken_Name": token.Name, "AMCreateToken_Digits": token.Digits});
    }
    for (idx in to_update) {
        token = to_update[idx];
        QueueAppMessage({"AMUpdateToken": [token.ID, 0, token.Name, 0]});
    }
    QueueAppMessage({"AMSetTokenListOrder": new_ids});
    Tokens = newTokens.map(function(e){e.Secret = null; return e;}); // Strip out the secrets so they don't appear in further log messages.
};

Pebble.addEventListener("webviewclosed", function(e) {
    if (!e.response) return;
    var newTokens = JSON.parse(decodeURIComponent(e.response));
    for (var idx in newTokens) {
        newTokens[idx].ID = +newTokens[idx].ID; // Blegh
    }
    ReconcileConfiguration(newTokens);
});

// slightly modified atob implementation from https://github.com/davidchambers/Base64.js
(function(){function t(t){this.message=t}var e="undefined"!=typeof exports?exports:this,r="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";t.prototype=Error(),t.prototype.name="InvalidCharacterError",e.atob||(e.atob=function(e){if(e=e.replace(/=+$/,""),1==e.length%4)throw new t("'atob' failed: The string to be decoded is not correctly encoded.");for(var o,n,a=0,i=0,c="";n=e.charAt(i++);~n&&(o=a%4?64*o+n:n,a++%4)?c+=String.fromCharCode(255&o>>(6&-2*a)):0)n=r.indexOf(n);return c})})();