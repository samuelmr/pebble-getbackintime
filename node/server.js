var express = require('express');  // import the express module

var Timeline = require('pebble-api');

// create a new Timeline object with our API key passed as an environment variable
var timeline = new Timeline();

var app = express(); // create an express instance
app.use(express.bodyParser());
app.set('port', (process.env.PORT || 5000)); // set the port on the instance

// add new place into history
app.post('/:userToken/place/new', function(req, res) {
  var userToken = req.params.userToken;
  var body = req.body;
  var pos = body.position || {};
  if (!pos.coords.latitude || !pos.coords.longitude) {
    if (req.param('latitude') && req.param('longitude')) {
      pos.coords = {
        'latitude': req.param('latitude'),
        'longitude': req.param('longitude')
      }
    }
    else {
      var err = 'No coordinates found in parameters!';
      res.status(400);
      res.send(err);
    }
  }
  var defaultTitle = '';
  var pin = body.pin || new Timeline.Pin({
    time: new Date(),
    layout: new Timeline.Pin.Layout({
      type: Timeline.Pin.LayoutType.GENERIC_PIN,
      tinyIcon: Timeline.Pin.Icon.PIN,
      title: defaultTitle
    })
  });
  pin.id = parseInt(Math.random() * (999999 - 100000) + 100000).toString(), // random pin id

  if (!body.pin) {
  }
  // store body to db
}

app.all('/:userToken/place/new', function(req, res) {
  var userToken = req.params.userToken;
  
  var pin = new Timeline.Pin({
    id: parseInt(Math.random() * (999999 - 100000) + 100000).toString(), // random pin id
    time: new Date(new Date().getTime() + 60*60*1000), // current date and time plus 1 hour
    layout: new Timeline.Pin.Layout({
      type: Timeline.Pin.LayoutType.GENERIC_PIN,
      tinyIcon: Timeline.Pin.Icon.PIN,
      title: 'Pin from Heroku!'
    })
  });
  
  var place = {id: id, pin: pin};


  timeline.sendUserPin(userToken, pin, function (err, body, resp) {
    if(err) {
      res.status(400);
      res.send(err);
    } else {
      res.send('Pin successfully pushed!');
    }
  });
});

app.get('/:userToken/place/:id', function(req, res) {
  var userToken = req.params.userToken;
  var id = req.params.id;
  // get from db
  // return
}


// start the webserver
var server = app.listen(app.get('port'), function () {
  console.log('Get Back server listening on port %s', app.get('port'));
});