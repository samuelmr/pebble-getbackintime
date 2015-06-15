var express = require('express');
var bodyParser = require('body-parser');
var Timeline = require('pebble-api');
var MongoClient = require('mongodb').MongoClient;
var exphbs  = require('express-handlebars');

var mongoUri = process.env.MONGOLAB_URI || 
  process.env.MONGOHQ_URL || 
  'mongodb://localhost/mydb'; 

// create a new Timeline object with our API key passed as an environment variable
var timeline = new Timeline();

var app = express();
app.use(bodyParser.json());

var hbs = exphbs.create({
  defaultLayout: 'main',
});

app.engine('handlebars', hbs.engine);
app.set('view engine', 'handlebars');

app.use(express.static(__dirname + '/public'));
app.set('port', (process.env.PORT || 5000)); // set the port on the instance

Date.prototype.isLeapYear = function() {
  var year = this.getFullYear();
  if((year & 3) != 0) return false;
  return ((year % 100) != 0 || (year % 400) == 0);
};

var dayCount = [0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334];

function createId() {
  var d = new Date();
  var mn = d.getMonth();
  var dn = d.getDate();
  var dayOfYear = dayCount[mn] + dn;
  if(mn > 1 && d.isLeapYear()) {
    dayOfYear++;
  }
  if (dayOfYear > 182) {
    dayOfYear -= 182;
  }
  var cs = d.getHours() * 60 * 60 * 100 +
    d.getMinutes() * 60 * 100 +
    d.getSeconds() * 100 +
    Math.floor(d.getMilliseconds()/10);
  return '' + dayOfYear.toString() + cs;
}

function pushPin(place, res) {
  var pin;
  // var pinID = parseInt(place._id.toHexString(), 16);
  var pinID = place._id.toString();
  if (place.pin) {
    place.pin.id = pinID;
    place.pin.time = new Date(place.pin.time);
    try {
     pin = new Timeline.Pin(place.pin);
    }
    catch(e) {
      console.warn('Failed to create pin: ' + e);
      res.status(400);
      res.send('Failed to create pin: ' + e);
      return;
    }
  }
  else {
    pin = new Timeline.Pin({
      id: pinID,
      time: new Date(),
      layout: new Timeline.Pin.Layout({
        type: Timeline.Pin.LayoutType.GENERIC_PIN,
        tinyIcon: Timeline.Pin.Icon.PIN,
        title: 'Get Back target',
        body: 'Set from watch'
      })
    });
  }
  pin.addAction(new Timeline.Pin.Action({
    title: 'Get Back',
    type: Timeline.Pin.ActionType.OPEN_WATCH_APP,
    launchCode: parseInt(place._id)
  }));
  pin.createNotification = new Timeline.Pin.Notification(
    {
      "layout": {
        "type": "genericNotification",
        "title": pin.layout.title || "Get Back target",
        "tinyIcon": "system://images/NOTIFICATION_FLAG",
        "body": "A new target added from watch."
      }
    }
  );

  place.pin = pin;
  // console.log('Sending pin: ' + JSON.stringify(pin));
  timeline.sendUserPin(place.user, pin, function (err, body, resp) {
    if(err) {
      console.warn('Failed to push pin to timeline: ' + err);
      res.status(400);
      res.send('Failed to push pin to timeline: ' + err);
    }
    else {
      console.log('Pin successfully pushed!');
      res.json(place);
    }
  });
}

// add new place into history
app.post('/:userToken/place/new', function(req, res) {
  // console.log(req.body);
  var userToken = req.params.userToken;
  // var place = JSON.parse(req.body) || {};
  var place = req.body || {};
  place.user = userToken;
  // if id is specified in params try to use it as mondodb id
  // otherwise create a new id, just use milliseconds since a nearby epoch...
  // This may result to duplicates!
  place._id = parseInt(place.id) || createId();
  place.time = new Date().getTime();
  if (place.pin && place.pin.time && Date.parse(place.pin.time)) {
    place.time = new Date(place.pin.time);
  }
  var pos = place.position; // || {};
  if (!pos || !pos.coords || !pos.coords.latitude || !pos.coords.longitude) {
    if (req.param('latitude') && req.param('longitude')) {
      pos.coords = {
        'latitude': req.param('latitude'),
        'longitude': req.param('longitude')
      }
      place.pos = pos;
    }
    else {
      var errorText = 'No coordinates found in parameters!';
      console.warn(errorText);
      res.status(400);
      res.send(errorText);
      return;
    }
  }

  // store place to db
  // console.log('Trying to connect to mongodb at ' + mongoUri);
  MongoClient.connect(mongoUri, function(err, db) {
    if (err || !db) {
      console.warn('Failed to open db connection: ' + err);
      res.status(400);
      res.send('Failed to open db connection: ' + err);
      return;
    }
    db.collection('places', function(err, collection) {
      if (err || !collection) {
        console.warn('Could not find collection "places": ' + err);
        res.status(400);
        res.send('Could not find collection "places": ' + err);
        return;
      }
      collection.insert(place, {safe: true}, function(err, result) {
        if (err || !result) {
          if (err.code == 11000) {
            console.warn('Failed to save place into db: ' + err);
            place._id++;
            // just one retry, no infinite loop...
            collection.insert(place, {safe: true}, function(err, result) {
              if (err || !result) {
                console.warn('Failed again to save place into db: ' + err);
                res.status(400);
                res.send('Failed to save place into db: ' + err);
                return;
              }
              console.log('Place saved to db! id = ' + place._id);
              pushPin(place, res);
            });
          }
          else {
            res.status(400);
            res.send('Failed to save place into db: ' + err);
            return;
          }
        }
        else {
          console.log('Place saved to db! id = ' + place._id);
          pushPin(place, res);
        }
      });
    });
  });
});

// get a certain point
app.get('/:userToken/place/:id', function(req, res) {
  var userToken = req.params.userToken;
  var id = parseInt(req.params.id);
  // console.log('Trying to connect to mongodb at ' + mongoUri);
  MongoClient.connect(mongoUri, function(err, db) {
    if (err) {
      console.warn('Failed to open db connection: ' + err);
      res.status(400);
      res.send('Failed to open db connection: ' + err);
      return;
    }
    db.collection('places', function(er, collection) {
      collection.findOne({_id: id}, function(err, doc) {
        if (err) {
          res.status(400);
          res.send('Failed to retrieve place from db: ' + err);
          return;
        }
        // console.log(doc);
        if (!doc || (doc.user != userToken)) {
          res.status(403);
          res.send('No place with id ' + id + ' found for user token ' + userToken);
          return;
        }
        res.json(doc);
      });
    });
  });
});

// get last :max points from history
app.get('/:userToken/places/:max?', function(req, res) {
  var userToken = req.params.userToken;
  var max = parseInt(req.params.max);
  // console.log('Trying to connect to mongodb at ' + mongoUri);
  MongoClient.connect(mongoUri, function(err, db) {
    if (err) {
      console.warn('Failed to open db connection: ' + err);
      res.status(400);
      res.send('Failed to open db connection: ' + err);
      return;
    }
    db.collection('places', function(er, collection) {
      var query = {"user": userToken, "_id": {$lte: Math.pow(2, 32)-1}};
      collection.find(query).sort({time: -1}).limit(max).toArray(function(err, places) {
        if (err) {
          console.warn('Failed to retrieve places from db: ' + err);
          res.status(400);
          res.send('Failed to retrieve places from db: ' + err);
          return;
        }
        if (places) {
          res.json(places);
        }
      });
    });
  });  
});

// show configuration page
app.get('/:userToken/configure', function(req, res) {
  var userToken = req.params.userToken;
  var context = {'token': userToken,
                 'return_to': req.param.return_to || 'pebblejs://close#'};
  if (req.param.conf) {
    context.conf = JSON.parse(req.param.conf);
  }
  res.render('configure', context);
});

app.get('/:userToken?/', function (req, res) {
  var context = {'token': req.params.userToken};
  res.render('home', context);
});

// start the webserver
var server = app.listen(app.get('port'), function () {
  console.log('Get Back server listening on port %s', app.get('port'));
});