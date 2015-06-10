var initialized = false;
var interval;
var lat1 = 0;
var lon1 = 0;
var lat2;
var lon2;
var speed = 0;
var accuracy = 0;
var phoneHead = 0;
var prevHead = 0;
var prevDist = 0;
var head = 0;
var dist = 0;
var token = '-';
var units = "metric";
var sens = 5;
var R = 6371000; // m
var locationWatcher;
var locationInterval;
var locationOptions = {timeout: 15000, maximumAge: 1000, enableHighAccuracy: true };
var serverAddress = 'http://getback.pebbletime.io/';
// var geocoder = 'http://nominatim.openstreetmap.org/reverse?format=json&zoom=18';
// var geocoder = 'http://api.geonames.org/findNearestAddressJSON?formatted=false&style=full';
var geocoder = 'http://services.gisgraphy.com/street/search?format=json&from=0&to=1';

Pebble.addEventListener("ready", function(e) {
  lat2 = parseFloat(localStorage.getItem("lat2")) || null;
  lon2 = parseFloat(localStorage.getItem("lon2")) || null;
  interval = parseInt(localStorage.getItem("interval")) || 0;
  units = localStorage.getItem("units") || "metric";
  sens = parseInt(localStorage.getItem("sens")) || 5;
  token = localStorage.getItem("token") || token;
  if ((lat2 === null) || (lon2 === null)) {
    storeCurrentPosition();
  }
  else {
    // console.log("Target location known:" + lon2 + ',' + lat2);
  }
  startWatcher();
  if (Pebble.getTimelineToken) {
    Pebble.getTimelineToken(
      function (timelineToken) {
        token = timelineToken;
        console.log('Got timeline token ' + token);
        localStorage.setItem("token", token);
        // sending id from phone to watch is just a signal that
        // the phone is ready to accept app messages (e.g. id)
        var msg = {"id": -1};
        sendMessage(msg);
      },
      function (error) { 
        console.log('Error getting timeline token: ' + error);
      }
    );
  }
  console.log("JavaScript app ready and running!");
  initialized = true;
});

Pebble.addEventListener("appmessage",
  function(e) {
    if (e && e.payload && e.payload.cmd) {
      console.log("Got command: " + e.payload.cmd);
      switch (e.payload.cmd) {
        case 'set':
          if (!e.payload.id || (e.payload.id < 0)) {
            storeCurrentPosition();
          }
          else {
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
        default:
          console.log("Unknown command!");
      }
    }
  }
);

Pebble.addEventListener("showConfiguration",
  function() {
    var conf = {
      'units': units,
      'interval': interval,
      'sens': sens};
    if (token != '-') {
      conf.token = token;
    }
    var uri = serverAddress + token + '/configure?conf=' +
      encodeURIComponent(JSON.stringify(conf));
    console.log("Configuration url: " + uri);
    Pebble.openURL(uri);
  }
);

Pebble.addEventListener("webviewclosed",
  function(e) {
    // console.log(e.response);
    var options = JSON.parse(decodeURIComponent(e.response));
    console.log("Webview window returned: " + JSON.stringify(options));
    units = (options.units == 'imperial') ? 'imperial' : 'metric';
    localStorage.setItem("units", units);
    console.log("Units set to: " + units);
    interval = parseInt(options.interval) || 0;
    localStorage.setItem("interval", interval);
    console.log("Interval set to: " + interval);
    sens = parseInt(options.sens) || 5;
    localStorage.setItem("sens", sens);
    console.log("Sentitivity set to: " + sens);
    var msg = {"units": units,
               "sens": parseInt(sens)};
    sendMessage(msg);
    calculate();
    startWatcher();
  }
);

function sendMessage(dict) {
  Pebble.sendAppMessage(dict, appMessageAck, appMessageNack);
  console.log("Sent message to Pebble! " + JSON.stringify(dict));
}

function appMessageAck(e) {
  console.log("Message accepted by Pebble!");
}

function appMessageNack(e) {
  console.log("Message rejected by Pebble! ");
}

function locationSuccess(position) {
  // console.log("Got location " +
  // position.coords.latitude + ',' +
  // position.coords.longitude + ', heading at ' +
  // position.coords.heading);
  lat1 = position.coords.latitude;
  lon1 = position.coords.longitude;
  speed = position.coords.speed;
  accuracy = position.coords.accuracy;
  phoneHead = position.coords.heading;
  calculate();
}

function addLocation(position) {
  storeLocation(position);
  if (token && (token != '-')) {
    // get address
    var addr = position.coords.latitude + ',' + position.coords.longitude;
    var url = geocoder + '&username=' + token + '&lat=' +
      position.coords.latitude + '&lng=' +
      position.coords.longitude;
    var rgc = new XMLHttpRequest(); // xhr for reverse geocoding, only one instance!
    rgc.open("get", url, true);
    rgc.setRequestHeader('User-Agent', 'Get Back in Time/1.7');
    // rgc.setRequestHeader('X-Forwarded-For', token);
    rgc.onerror = rgc.ontimeout = function(e) {
      console.log("Reverse geocoding error: " + this.statusText);
    }
    rgc.onload = function(res) {
      var json = this.responseText;
      console.log("Got: " + json + " from reverse geocoder");
      var latlon = Math.round(position.coords.latitude*1000)/1000 + ',' +
                   Math.round(position.coords.longitude*1000)/1000;
      var obj = {
        "position": position,
        "pin": {
          "time": new Date().toISOString(),
          "layout": {
            "type": "genericPin",
            "tinyIcon": "system://images/NOTIFICATION_FLAG_TINY",
            "title": latlon,
            "subtitle": "Get Back in Time",
            "body": "Set from watch."
          },
        }
      }
      var res = JSON.parse(json);
      if (res && res.result && res.result[0]) {
        var placeName = res.result[0].name || '';
        if (!placeName && res.result[0].isIn) {
          placeName = res.result[0].isIn;
        }
        console.log("Reverse geocoded address: " + placeName);
        obj.pin.layout.title = placeName;
      };
      // add place to server
      var url = serverAddress + token + '/place/new';
      var req = new XMLHttpRequest();
      req.onload = function(res) {
        var json = this.responseText;
        console.log('Got ' + json);
        if (!json || (json.substr(0, 1) != '{')) {
          console.log("Error sending place to server: " + json);
          return;
        }
        var obj = JSON.parse(json); 
        console.log("Sent place to server: " + obj._id);
      };
      req.open("post", url, true);
      req.setRequestHeader('Content-Type', 'application/json');
      req.send(JSON.stringify(obj));
    };
    rgc.send(null);
    console.log("Trying to reverse geocode: " + url);
  }
}

function storeLocation(position) {
  lat2 = position.coords.latitude;
  lon2 = position.coords.longitude;
  localStorage.setItem("lat2", lat2);
  localStorage.setItem("lon2", lon2);
  // console.log("Stored location " +
  // position.coords.latitude + ',' +
  // position.coords.longitude);
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
    var dLat = toRad(lat2-lat1);
    var dLon = toRad(lon2-lon1);
    var l1 = toRad(lat1);
    var l2 = toRad(lat2);
    var a = Math.sin(dLat/2) * Math.sin(dLat/2) +
            Math.sin(dLon/2) * Math.sin(dLon/2) * Math.cos(l1) * Math.cos(l2); 
    var c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a)); 
    dist = Math.round(R * c);
    var y = Math.sin(dLon) * Math.cos(l2);
    var x = Math.cos(l1)*Math.sin(l2) -
            Math.sin(l1)*Math.cos(l2)*Math.cos(dLon);
    head = toDeg(Math.round(Math.atan2(y, x)));
    if ((dist != prevDist) || (head != prevHead)) {
      msg = {"dist": dist,
             "head": head,
             "accuracy": accuracy,
             "speed": speed,
             "phonehead": phoneHead,
             "units": units,
             "sens": parseInt(sens)};
      sendMessage(msg);
      prevDist = dist;
      prevHead = head;
    }
  }
}

function locationError(error) {
  console.warn('location error (' + error.code + '): ' + error.message);
}

function storeCurrentPosition() {
  // console.log("Attempting to store current position.");
  var msg = {"dist": 0,
             "head": 0};
  sendMessage(msg);
  navigator.geolocation.getCurrentPosition(addLocation, locationError, locationOptions);
}

function getPositionFromServer(id) {
  var url = serverAddress + token + '/place/' + id;
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
  console.log('Got position from server: ' +
    obj.position.coords.latitude + ', ' +
    obj.position.coords.longitude);
  storeLocation(obj.position);
}

function startWatcher() {  
  if (locationInterval) {
    clearInterval(locationInterval);
  }
  if (locationWatcher) {
    navigator.geolocation.clearWatch(locationWatcher);
  }
  if (interval > 0) {
    console.log('Interval is ' + interval + ', using getCurrentPosition and setInterval');
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    locationInterval = setInterval(function() {
      navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    }, interval * 1000);      
  }
  else {
    console.log('Interval not set, using watchPosition');
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
    locationWatcher = navigator.geolocation.watchPosition(locationSuccess, locationError, locationOptions);  
    console.log("Started location watcher: " + locationWatcher);
  }
  // for testing: randomize movement!
  /*
  window.navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
  if (!interval) {
    interval = 10;
  }
  locationWatcher = setInterval(function() {
    lat1 = lat1 + Math.random()/100 * ((Math.random() > 0.5) || -1);
    lon1 = lon1 - Math.random()/100 * ((Math.random() > 0.5) || -1);
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
