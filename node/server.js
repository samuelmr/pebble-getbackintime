var express = require('express');
var bodyParser = require('body-parser');
var Timeline = require('pebble-api');
var MongoClient = require('mongodb').MongoClient;

var mongoUri = process.env.MONGOLAB_URI || 
  process.env.MONGOHQ_URL || 
  'mongodb://localhost/mydb'; 

// create a new Timeline object with our API key passed as an environment variable
var timeline = new Timeline();

var app = express(); // create an express instance
app.use(express.static(__dirname + '/public'));
app.use(bodyParser.json());
app.set('port', (process.env.PORT || 5000)); // set the port on the instance

// add new place into history
app.post('/:userToken/place/new', function(req, res) {
  var userToken = req.params.userToken;
  var place = req.body || {};
  place.user = userToken;
  var pos = place.position || {};
/*
  if (!pos.coords.latitude || !pos.coords.longitude) {
    if (req.param('latitude') && req.param('longitude')) {
      pos.coords = {
        'latitude': req.param('latitude'),
        'longitude': req.param('longitude')
      }
      place.pos = pos;
    }
    else {
      var err = 'No coordinates found in parameters!';
      res.status(400);
      res.send(err);
    }
  }
*/

  // store place to db
  console.log('Trying to connect to mongodb at ' + mongoUri);
  MongoClient.connect(mongoUri, function(err, db) {
    if (err) {
      res.status(400);
      res.send('Failed to open db connection: ' + err);
      return;
    }
    db.collection('places', function(er, collection) {
      collection.insert(place, {safe: true}, function(err, doc) {
        if (err) {
          res.status(400);
          res.send('Failed to save place into db: ' + err);
          return;
        }
        console.log('Place saved to db!');

        // push a pin into timeline
        var pin;
        if (doc.pin) {
          pin = doc.pin;
          pin.id = doc._id; // .toHexString()
        }
        else {
          pin = new Timeline.Pin({
            id: doc._id, // .toHexString()
            time: new Date(),
            layout: new Timeline.Pin.Layout({
              type: Timeline.Pin.LayoutType.GENERIC_PIN,
              tinyIcon: Timeline.Pin.Icon.PIN,
              title: 'Get Back target',
              body: 'Set from watch'
            })
          });
        }
        timeline.sendUserPin(userToken, pin, function (err, body, resp) {
          if(err) {
            res.status(400);
            res.send('Failed to push pin to timeline: ' + err);
            return;
          } else {
            console.log('Pin successfully pushed!');
          }
        });

        res.json(doc);
      });
    });
  });
  res.status(500);
  res.send('Internal server errr: ');
  return;
});

app.get('/:userToken/place/:id', function(req, res) {
  var userToken = req.params.userToken;
  var id = req.params.id;
  console.log('Trying to connect to mongodb at ' + mongoUri);
  MongoClient.connect(mongoUri, function(err, db) {
    if (err) {
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
        if (!doc || (doc.user != userToken)) {
          res.status(403);
          res.send('No place with id ' + id + ' found for user token ' + userToken);
          return;
        }
        res.json(doc);
      });
    });
  });
});

app.get('/:userToken/configure', function(req, res) {
  var userToken = req.params.userToken;
  var conf = req.param.conf;
  var ret = req.param.return_to;
  // possibly get saved configuration from DB and override conf?
  var configURL = '/configure.html?return_to=' + ret + '#' + conf;
  res.redirect(configURL);
});

// start the webserver
var server = app.listen(app.get('port'), function () {
  console.log('Get Back server listening on port %s', app.get('port'));
});