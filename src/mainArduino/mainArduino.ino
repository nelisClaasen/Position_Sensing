#include <Arduino.h>
#include <sstream>
#include <string>

//Encoder Pins.
const int xAxisA = 34;
const int xAxisB = 35;

const int yAxisA = 32;
const int yAxisB = 33;

const int HIGH_THRESHOLD = 1500;
const int LOW_THRESHOLD = 800;

//Encoder click counter.
long x_cw_clicks = 0, x_ccw_clicks = 0;
long y_cw_clicks = 0, y_ccw_clicks = 0;

//Keep track of previous encoder values.
bool xStateA = 0, xStateB = 0;
bool yStateA = 0, yStateB = 0;

//UART connects to other subsystems.
HardwareSerial mainHub(0);
const int mainHubRx = 0;
const int mainHubTx = 0;

HardwareSerial cadInterpreter(1);
const int cadIntRx = 0;
const int cadIntTx = 0;

HardwareSerial motorControl(2);
const int motorContRx = 0;
const int motorContTx = 0;

int baudRateESP = 115200;
int baudRatePIC = 9600;

//Calibration data.
float mm_per_click_x = 1.7; //default value of 1
float mm_per_click_y = 1.7; //default value of 1
float boardWidth = 0; //in mm
float boardHeight = 0; //in mm

//State tracking variables.
//Variables needed to track calibration state.
bool calibrating = false;
bool cal_width = false;
bool cal_height = false;
//Sending automatic coordinates.
bool sending_auto = false;

//Variables to keep track of automatic mode's commands to send (after they have been translated over to steps)
int stepsPerClickX = 10;
int stepsPerClickY = 10;
float autoStepsX = 0;
float autoStepsY = 0;
bool autoZ = 0;

//Function to determine which direction encoder turned, updating the state and click values.
void updateEncoder(int pinA, int pinB, long &cw_clicks, long &ccw_clicks, bool &stateA, bool &stateB)
{
  int rawA = analogReadMilliVolts(pinA);
  int rawB = analogReadMilliVolts(pinB);

  bool tempA = stateA;
  bool tempB = stateB;

  //Update the state of pinA
  if(rawA > HIGH_THRESHOLD)
  {
    tempA= 1;
  }
  else if(rawA < LOW_THRESHOLD)
  {
    tempA = 0;
  }

  //Update the state of pinB
  if(rawB > HIGH_THRESHOLD)
  {
    tempB = 1;
  }
  else if(rawB < LOW_THRESHOLD)
  {
    tempB = 0;
  }

  //figure out in what direction we turned.
  if(tempA != stateA)
  {
    if(tempA)
    {
      if(!tempB)
      {
        cw_clicks++;
      }
      else
      {
        ccw_clicks++;
      }
    }
  }

  stateA = tempA;
  stateB = tempB;
}

//Function to receive a command from the cad interpreter sub system.
void receiveAuto()
{
  //First send ready command to cad interpreter subsystem.
  cadInterpreter.println("9, 0");

  //Wait until message is available.
  while(!(cadInterpreter.available() > 0))
  {
  }

  String received = cadInterpreter.readStringUntil('\n');
  std::istringstream stream(received.c_str());
  std::string value_str;

  std::getline(stream, value_str, ',');
  autoZ = std::atof(value_str.c_str());

  std::getline(stream, value_str, ',');
  autoStepsX = floor(std::atof(value_str.c_str())/mm_per_click_x) * stepsPerClickX - autoStepsX;

  std::getline(stream, value_str, ',');
  autoStepsY = floor(std::atof(value_str.c_str())/mm_per_click_y) * stepsPerClickY - autoStepsY;
}

//Function to send the interpreted command to the motor control sub-system.
bool sendMotor()
{
  //First send data.
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%d, %.4f, %.4f", autoZ, autoStepsX, autoStepsY);
  motorControl.println(String(buffer));

  //Wait for done confirmation.
  while(!(motorControl.available() > 0))
  {
  }

  String received = motorControl.readStringUntil('\n');
  received.trim();

  std::istringstream stream(received.c_str());
  std::string command;

  std::getline(stream, command, ',');

  if(command == "D")
  {
    return true;
  }
  
  return false;
}

void setup() {
  //Setup serial communication.
  mainHub.begin(baudRateESP, SERIAL_8N1, mainHubRx, mainHubTx);
  cadInterpreter.begin(baudRateESP, cadIntRx, cadIntTx);
  motorControl.begin(baudRatePIC, motorContRx, motorContTx);
}

void loop() {
  //Check for communcations from mainHub.
  if(mainHub.available() > 0)
  {
    String received = mainHub.readStringUntil('\n');
    received.trim();

    std::istringstream stream(received.c_str());
    std::string command;
    std::string value_str;
    double value;

    std::getline(stream, command, ',');
    std::getline(stream, value_str, ',');
    value = std::atof(value_str.c_str());

    if(command == "C") //Check to see if calibration has started.
    {
      //Reset current clicks.
      x_ccw_clicks = 0;
      x_cw_clicks = 0;

      y_ccw_clicks = 0;
      y_cw_clicks = 0;

      //Reset current conversion rates.
      mm_per_click_x = 1.7;
      mm_per_click_y = 1.7;

      calibrating = true;
      cal_width = true;
    }
    else if(command == "W") //Get board width.
    {
      boardWidth = value;
    }
    else if(command == "H") //Get board height.
    {
      boardHeight = value;
    }
    else if(command == "X") //Check to see if width calibration is done.
    {
      mm_per_click_x = boardWidth/abs(x_cw_clicks - x_ccw_clicks);
      cal_width = false;
      cal_height = true;
    }
    else if(command == "Y") //Check to see if height calibration is done.
    {
      mm_per_click_y = boardHeight/abs(y_cw_clicks - y_ccw_clicks);
      cal_height = false;
      calibrating = false;
    }
    else if(command == "A") //Check to see if we have initiated automatic mode.
    {
      sending_auto = true;
    }
  }

  //If currently calibrating.
  if(calibrating)
  {
    if(cal_width)
    {
      updateEncoder(xAxisA, xAxisB, x_cw_clicks, x_ccw_clicks, xStateA, xStateB);
    }
    else if(cal_height)
    {
      updateEncoder(yAxisA, yAxisB, y_cw_clicks, y_ccw_clicks, yStateA, yStateB);
    }
  }
  else if(sending_auto)
  {
    //Get next command from automatic subsystem.
    receiveAuto();

    //Send command to motor subsystem.
    sendMotor();
  }
}
