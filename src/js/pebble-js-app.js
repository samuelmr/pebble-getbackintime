var initialized = false;
var interval;
var lat1 = 0;
var lon1 = 0;
var lat2;
var lon2;
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
  console.log("Message rejected by Pebble! " + e.error);
}

function locationSuccess(position) {
  // console.log("Got location " + position.coords.latitude + ',' + position.coords.longitude + ', heading at ' + position.coords.heading);
  lat1 = position.coords.latitude;
  lon1 = position.coords.longitude;
  calculate();
}

function addLocation(position) {
  storeLocation(position);
  if (token && (token != '-')) {
    // add place to server
    var obj = {position: position};
    var url = serverAddress + token + '/place/new';
    var req = new XMLHttpRequest();
    req.onload = function(res) {
      var json = this.responseText;
      var obj = JSON.parse(json); 
      console.log("Sent place to server: " + obj._id);
    };
    req.open("post", url, true);
    req.setRequestHeader('Content-Type', 'application/json');
    req.send(JSON.stringify(obj));
  }
}

function storeLocation(position) {
  lat2 = position.coords.latitude;
  lon2 = position.coords.longitude;
  localStorage.setItem("lat2", lat2);
  localStorage.setItem("lon2", lon2);
  // console.log("Stored location " + position.coords.latitude + ',' + position.coords.longitude);
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

function parseServerResponse(res) {
  if (!res.position) {
    console.warn('No position from server');
    return false;
  }
  console.log('Got position from server: ' + res.position.latitude + ', ' + res.position.longitude);
  storeLocation(res.position);
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
  */
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
}
function toRad(num) {
  return num * Math.PI / 180;  
}
function toDeg(num) {
  return num * 180 / Math.PI;
}
