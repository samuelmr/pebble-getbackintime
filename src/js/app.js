var MAX_PLACES = 20; // history length
var MAX_ERRORS = 5;
var TTL = 500; // time to live for app messages in milliseconds
var initialized = false;
var lat1 = 0;
var lon1 = 0;
var lat2 = parseFloat(localStorage.getItem("lat2")) || null;
var lon2 = parseFloat(localStorage.getItem("lon2")) || null;
var interval = parseInt(localStorage.getItem("interval")) || 0;
var units = localStorage.getItem("units") || "metric";
var sens = parseInt(localStorage.getItem("sens")) || 5;
var userToken = localStorage.getItem("userToken") || "-";
var timelineToken = localStorage.getItem("timelineToken") || "";
var speed = 0;
var accuracy = 0;
var phoneHead = 0;
var prevHead = 0;
var prevDist = 0;
var R = 6371000; // m
var locationWatcher;
var locationInterval;
var locationOptions = {timeout: 15000, maximumAge: 1000, enableHighAccuracy: true };
var serverAddress = 'http://getback.timelinepush.com/';
// var geocoder = 'http://nominatim.openstreetmap.org/reverse?format=json&zoom=18';
// var geocoder = 'http://api.geonames.org/findNearestAddressJSON?formatted=false&style=full';
var geocoder = 'http://services.gisgraphy.com/street/search?format=json&from=0&to=1';
var messageQueue = [];
var errorCount = 0;
var processing = 0;
var months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

Pebble.addEventListener("ready", function(e) {
  console.log('JS ready');
  userToken = Pebble.getAccountToken() || "-";
  localStorage.setItem("userToken", userToken);
  if ((lat2 === null) || (lon2 === null)) {
    storeCurrentPosition();
  }
  else {
    // console.log("Target location known:" + lon2 + ',' + lat2);
  }
  startWatcher();
  if (Pebble.getTimelineToken) {
    Pebble.getTimelineToken(
      function (token) {
        timelineToken = token;
        // console.log('Got timeline token ' + timelineToken);
        localStorage.setItem("timelineToken", timelineToken);
      },
      function (error) {
        // console.log('Error getting timeline token: ' + error);
      }
    );
  }
  else {
    console.warn('Timeline not supported (no timeline token)');
  }
  // sending id from phone to watch is just a signal that
  // the phone is ready to accept app messages (e.g. id)
  var msg = {"id": -1};
  messageQueue.push(msg);
  sendNextMessage();
  getHistoryFromServer();
  initialized = true;
  // console.log("JavaScript app ready and running!");
});

Pebble.addEventListener("appmessage",
  function(e) {
    if (e && e.payload && e.payload.cmd) {
      // console.log("Got command: " + e.payload.cmd + ' (' + e.payload.id + ')');
      switch (e.payload.cmd) {
        case 'set':
          if (!e.payload.id || (e.payload.id < 0)) {
            // console.log('Setting new target (' + e.payload.id + ')');
            storeCurrentPosition();
          }
          else {
            // console.log('Getting data for place ' + e.payload.id);
            getPositionFromServer(e.payload.id);
          }
          break;
        case 'clear':
          lat2 = null;
          lon2 = null;
          break;
        case 'quit':
          navigator.geolocation.clearWatch(locationWatcher);
          break;
        case 'insert':
          addLocation({
            'coords': {
              'latitude': parseFloat(e.payload.latitude),
              'longitude': parseFloat(e.payload.longitude)
            }
          }, {
            'placeName': e.payload.placename,
            'body': e.payload.timelinebody
          });
          break;
        default:
          console.warn("Unknown command " + e.payload.cmd);
      }
    }
  }
);

Pebble.addEventListener("showConfiguration",
  function() {
    userToken = Pebble.getAccountToken() || userToken;
    var conf = {
      'units': units,
      'interval': interval,
      'sens': sens,
      'userToken': userToken
    };
    if (timelineToken) {
      conf.timelineToken = timelineToken;
    }
    var uri = serverAddress + userToken + '/configure?conf=' +
      encodeURIComponent(JSON.stringify(conf));
    console.log("Configuration url: " + uri);
    Pebble.openURL(uri);
  }
);

Pebble.addEventListener("webviewclosed",
  function(e) {
    // console.log(e.response);
    var options = JSON.parse(decodeURIComponent(e.response));
    // console.log("Webview window returned: " + JSON.stringify(options));
    units = options.units || 'metric';
    localStorage.setItem("units", units);
    // console.log("Units set to: " + units);
    interval = parseInt(options.interval) || 0;
    localStorage.setItem("interval", interval);
    // console.log("Interval set to: " + interval);
    if (interval > 1) {
      locationOptions.timeout = (interval - 1) * 1000;
    }
    sens = parseInt(options.sens) || 5;
    localStorage.setItem("sens", sens);
    // console.log("Sentitivity set to: " + sens);
    var msg = {
      "units": units,
      "sens": parseInt(sens)
    };
    messageQueue.push(msg);
    sendNextMessage();
    // console.log('was: ' + lat2 + ', ' + lon2 + ')');
    if (options.chosenPos) {
      var latlon = options.chosenPos.split(', ');
      lat2 = parseFloat(latlon[0]);
      lon2 = parseFloat(latlon[1]);
    }
    // console.log('is: ' + lat2 + ', ' + lon2 + ')');
    calculate();
    startWatcher();
    getHistoryFromServer();
  }
);

function sendNextMessage() {
  var now = new Date().getTime();
  var processed_time = now - processing;
  if (processing && (processed_time > TTL)) {
    console.warn('Message was processed more than ' + TTL + ' milliseconds (' + processed_time + '). Aborting.');
    messageQueue.shift();
    processing = 0;
  }
  else if (processing) {
    // console.log('Waiting for an earlier message, ' + processed_time + ' ms passed... (' + messageQueue.length + ' messages waiting!)');
    setTimeout(sendNextMessage, TTL-processed_time+20);
    return;
  }
  if ((messageQueue.length > 0) && messageQueue[0]) {
    // console.log('Sending message 1/' + messageQueue.length);
    Pebble.sendAppMessage(messageQueue[0], appMessageAck, appMessageNack);
    // console.log("Sent message to Pebble! " + messageQueue.length + ': ' + JSON.stringify(messageQueue[0]));
    processing = now;
  }
}

function appMessageAck(e) {
  processing = 0;
  errorCount = 0;
  messageQueue.shift();
  // console.log('Message accepted by Pebble! (' + messageQueue.length + ' messages waiting!)');
  sendNextMessage();
}

function appMessageNack(e) {
  processing = 0;
  if (e && e.data && e.data.transactionId) {
    // console.log("Rejected message id: " + e.data.transactionId);
  }
  if (errorCount >= MAX_ERRORS) {
    messageQueue.shift();
  }
  else {
    errorCount++;
    // console.log("Retrying, " + errorCount);
  }
  console.warn('Message rejected by Pebble! ' + e.error + ' (' + messageQueue.length + ' messages waiting!)');
  sendNextMessage();
}

function locationSuccess(position) {
  /*
  console.log("Got location " +
    position.coords.latitude + ',' +
    position.coords.longitude + ', heading at ' +
    position.coords.heading + ', accuracy ' +
    position.coords.accuracy);
  */
  lat1 = position.coords.latitude;
  lon1 = position.coords.longitude;
  speed = position.coords.speed;
  accuracy = position.coords.accuracy;
  phoneHead = position.coords.heading;
  calculate();
}

function setTimelinePin(coords, placeName, body) {
  var latlon = Math.round(coords.latitude * 1000) / 1000 + ',' +
    Math.round(coords.longitude * 1000) / 1000;
  var obj = {
    "position": {
      "coords": coords
    },
    "timelineToken": timelineToken,
    "pin": {
      "time": new Date().toISOString(),
      "layout": {
        "type": "genericPin",
        "tinyIcon": "system://images/NOTIFICATION_FLAG_TINY",
        "title": placeName || latlon,
        "subtitle": "Get Back in Time",
        "body": body || "Set from watch."
      }
    }
  };
  // add place to server
  var url = serverAddress + userToken + '/place/new';
  console.log('Adding place to ' + url + ': ' + JSON.stringify(obj));
  var req = new XMLHttpRequest();
  req.onload = function() {
    var json = this.responseText;
    // console.log('Got ' + json);
    if (!json || (json.substr(0, 1) != '{')) {
      console.warn("Error sending place to server: " + json);
      return;
    }
    //var obj = JSON.parse(json);
    // console.log("Sent place to server: " + obj._id);
    getHistoryFromServer();
  };
  req.open("post", url, true);
  req.setRequestHeader('Content-Type', 'application/json');
  req.send(JSON.stringify(obj));

/*
  // side effect of pushing timeline pin: also update appGlance
  var sliceObj = {
    "layout": {
      "icon": "app://images/LOGO_TINY",
      "subtitleTemplateString": obj.pin.layout.title
    }
  };
  setSlice(sliceObj);
*/

}

/*
function setSlice(obj) {
  if (Pebble.appGlanceReload) {
    Pebble.appGlanceReload([obj], appGlanceSuccess, appGlanceFailure);
  }
}

function appGlanceSuccess(appGlanceSlices, appGlanceReloadResult) {
  console.log('appGlance updated!');
}

function appGlanceFailure(appGlanceSlices, appGlanceReloadResult) {
  console.error('appGlance update FAILED!');
}

*/
function reverseGeocode(coords, callback) {
  // get address
  var url = geocoder + '&username=' + userToken + '&lat=' +
    coords.latitude + '&lng=' + coords.longitude;
  var rgc = new XMLHttpRequest(); // xhr for reverse geocoding, only one instance!
  rgc.open("get", url, true);
  rgc.setRequestHeader('User-Agent', 'Get Back in Time/3.21/' + userToken);
  rgc.onerror = rgc.ontimeout = function(e) {
    console.warn("Reverse geocoding error: " + this.statusText);
  };
  rgc.onload = function(res) {
    var json = this.responseText;
    // console.log("Got: " + json + " from reverse geocoder");

    if (json && (json.substr(0, 1) == '{')) {
      var gres = JSON.parse(json);
      if (gres && gres.result && gres.result[0]) {
        var placeName = gres.result[0].name || '';
        if (!placeName && gres.result[0].isIn) {
          placeName = gres.result[0].isIn;
        }
        // console.log("Reverse geocoded address: " + placeName);
        callback(placeName);
      }
    }
    else {
      console.warn("Reverse geocoding error: " + json);
      callback(null);
    }

  };
  // console.log("Trying to reverse geocode: " + url);
  rgc.send(null);
}

function addLocation(position, extra) {
  var coords = {
    'latitude': position.coords.latitude,
    'longitude': position.coords.longitude
  };
  storeLocation(position);

  if (extra && extra.placeName && extra.placeName.length) {
    setTimelinePin(coords, extra.placeName, extra.body);
  } else {
    if (userToken && (userToken != '-')) {
      reverseGeocode(coords, setTimelinePin.bind(null, coords));
    }
  }
}

function storeLocation(position) {
  lat2 = position.coords.latitude;
  lon2 = position.coords.longitude;
  localStorage.setItem("lat2", lat2);
  localStorage.setItem("lon2", lon2);
  /*
  console.log("Stored location " +
    position.coords.latitude + ',' +
    position.coords.longitude);
  */
  calculate();
}

function calculate() {
  var msg;
  if (lat2 || lon2) {
    if (!lat2) {
      lat2 = 0;
    }
    if (!lon2) {
      lon2 = 0;
    }

    var φ1 = toRad(lat1);
    var φ2 = toRad(lat2);
    var Δφ = toRad(lat2-lat1);
    var Δλ = toRad(lon2-lon1);

    var a = Math.sin(Δφ/2) * Math.sin(Δφ/2) +
            Math.cos(φ1) * Math.cos(φ2) *
            Math.sin(Δλ/2) * Math.sin(Δλ/2);
    var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
    var dist = Math.round(R * c);

    var y = Math.sin(Δλ) * Math.cos(φ2);
    var x = Math.cos(φ1)*Math.sin(φ2) -
            Math.sin(φ1)*Math.cos(φ2)*Math.cos(Δλ);
    var θ = Math.atan2(y, x);

    var head = Math.round((toDeg(θ)+360) % 360);

    if ((dist != prevDist) || (head != prevHead)) {
      msg = {
        "dist": dist,
        "head": head,
        "accuracy": accuracy,
        "phonehead": -1,
        "speed": 0,
        "units": units,
        "sens": parseInt(sens)
      };
      if ((speed > 0) && (!isNaN(phoneHead)) && (accuracy < 40)) {
        // enough movement and accuracy to calculate speed and heading
        msg.phonehead = phoneHead;
        msg.speed = speed;
      }
      messageQueue.push(msg);
      sendNextMessage();
      prevDist = dist;
      prevHead = head;
    }
    else {
      // console.log(dist + ' = ' + prevDist + ' and ' + head + ' = ' + prevHead + ', not enough movement');
    }
  }
}

function locationError(error) {
  console.warn('location error (' + error.code + '): ' + error.message);
}

function storeCurrentPosition() {
  // console.log("Attempting to store current position.");
  var msg = {
    "dist": 0,
    "head": 0
  };
  messageQueue.push(msg);
  sendNextMessage();
  navigator.geolocation.getCurrentPosition(addLocation, locationError, locationOptions);
}

function getPositionFromServer(id) {
  var url = serverAddress + userToken + '/place/' + id;
  var req = new XMLHttpRequest();
  req.onload = parseServerResponse;
  req.open("get", url, true);
  req.send();
}

function parseServerResponse() {
  var res = this.responseText;
  if (!res) {
    console.warn('No response from server');
    return false;
  }
  if ((this.status != 200) || (this.responseText.substr(0,1) != '{')) {
    console.warn('Server did not return a JSON object: ' + this.responseText);
    return false;
  }
  var obj = JSON.parse(res);
  if (!obj || !obj.position) {
    console.warn('No position from server: ' + this.responseText);
    return false;
  }
  /*
  console.log('Got position from server: ' +
     obj.position.coords.latitude + ', ' +
     obj.position.coords.longitude);
  */
  storeLocation(obj.position);
}

function getHistoryFromServer() {
  var url = serverAddress + userToken + '/places/' + MAX_PLACES;
  // console.log('Getting history from ' + url);
  var req = new XMLHttpRequest();
  req.onload = parseHistory;
  req.onerror = function(e) {
    console.warn('History error: ' + JSON.stringify(e));
  };
  req.open("get", url, true);
  req.send();
}

function parseHistory() {
  var res = this.responseText;
  // console.log('History: ' + res);
  if (!res) {
    console.warn('No history response from server');
    return false;
  }
  if ((this.status != 200) || (this.responseText.substr(0,1) != '[')) {
    console.warn('Server did not return a JSON array: ' + this.responseText);
    return false;
  }
  var places = JSON.parse(res);
  if (!places || !places.length) {
    console.warn('No location history: ' + this.responseText);
    return false;
  }
  // console.log('Got ' + places.length + ' history positions');
  var menuItemCount = 0;
  for (var i=0; i<places.length; i++) {
    if (!places[i]._id || places[i]._id < 0 || places[i]._id > Math.pow(2, 32) -1) {
      continue;
    }
    var dStr = '---';
    if (places[i].time) {
      var d = new Date(places[i].time);
      dStr = months[d.getMonth()] + ' ' +
             d.getDate() + ' ' +
             d.getHours() + ':' +
             (d.getMinutes() < 10 ? '0': '') +
             d.getMinutes();
    }
    var title = '---';
    if (places[i].pin && places[i].pin.layout && places[i].pin.layout.title) {
      title = places[i].pin.layout.title;
    }
    if ((dStr == '---') && (title == '---')) {
      continue;
    }
    var msg = {
      'id': places[i]._id,
      'index': menuItemCount,
      'count': ++menuItemCount,
      'title': dStr,
      'subtitle': title
    };
    messageQueue.push(msg);
  }
  // console.log('Pushed ' + messageQueue.length + ' messages to queue...');
  sendNextMessage();
}

function startWatcher() {
  if (locationInterval) {
    clearInterval(locationInterval);
    locationInterval = null;
  }
  if (locationWatcher) {
    navigator.geolocation.clearWatch(locationWatcher);
  }
  if (interval > 0) {
    // console.log('Interval is ' + interval + ', using getCurrentPosition and setInterval');
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    locationInterval = setInterval(function() {
      // console.log('Interval of ' + interval + ' seconds passed');
      navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    }, interval * 1000);
  }
  else {
    // console.log('Interval not set, using watchPosition');
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    locationWatcher = navigator.geolocation.watchPosition(locationSuccess, locationError, locationOptions);
    // console.log("Started location watcher: " + locationWatcher);
  }
  /*
  // for testing: randomize movement!
  window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
  if (!interval) {
    interval = 10;
  }
  locationWatcher = setInterval(function() {
    lat1 = lat1 + Math.random()/100 * ((Math.random() > 0.5) || -1);
    lon1 = lon1 - Math.random()/100 * ((Math.random() > 0.5) || -1);
    speed = Math.random() * 60;
    accuracy = Math.random() * 60;
    phoneHead = Math.random() * 360;
    console.log(lat1 + ', ' + lon1 + ', ' + speed + ', ' + accuracy);
    calculate();
  }, interval * 1000);
  console.log("Started fake location watcher: " + locationWatcher);
  */
}
function toRad(num) {
  return num * Math.PI / 180;
}
function toDeg(num) {
  return num * 180 / Math.PI;
}
