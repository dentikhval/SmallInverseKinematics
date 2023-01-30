/*
  Simple script to move my tiny 6dof robotic arm - well, it WAS simple until I got my hands on it :)

  Originally written by Skyentific but modified to be more flexible in terms of robot parameters,
  and perform IK and FK calculations from a given set of inputs.

  Robot is defined by a set of Denavit-Hartenberg parameters around line 130.

  Original functionality of controlling motors commented out, as it was badly implemented.
  Several important errors have been corrected, and a lot of criticism expressed (below) to Skyentific, especially in the motion control part.
  Instead of hand-written repetitive code all repetitive operations are within for loops, and all repetitive variables are converted to arrays.

  Heavily modified code from Skyentific by Denis Tikhonov.
*/


/*
  What this code does now:

  (1) Defines several positions that will be calculated in X[][6] 2D array;
  (2) For each joint:
  - Serial.print input array of cartesian coordinates
  - Calculates inverse kinematics and outputs an array of joint angles
  - Calculates forward kinematics for said joint angles and outputs the calculated (from joint angles) cartesian coordinates
  - Compares input cartesian coords to output of forward kinematics and outputs the calculated error.

  So far I am getting a stable round-trip error of less than 0.001 when feeding IK otput directly to FK input.

  For the cartesian X, Y and Z axes everything is very stable. For the ZYZ Euler angles I was getting weird numbers
  when values are equal to 90, -90, 180, -180 or anywhere beyond +- 180 degrees - this causes singularity which
  was not accounted for in original code. I added some hacks to get around that and prevent or catch errors.

  Singularity looks quite detectable by calculating error.
  Normally at 89.9 degrees max error is below 0.001 degree, however at 89.99 degrees the calculated error rises to 0.02, and at 90.0 the output goes haywire.

  They say, dealing with singularity is one of the more complex robotics problems. I see super simple ways to deal with it:
  Option 0 (mandatory) - limit the possible input values strictly between -180 and +180 degrees and convert any values beyond those to within limits
  Option 1 (mandatory) - take into account axes travel limits and reject moves that would lead to out of bounds
  Option 3 (dirty) - add or substract a little value from a 0.00 / 90.00 / 180.00 degree input silently just to avoid singularity
  Option 4 (clean)- detect high calculated error and reject command before executing completely. Can be circumvented by means of the above.
  All four are implemented in code below.

  Performance-wise, the IK and FK calculations take between 35 and 38 milliseconds on an Arduino Mega @ 16 MHz with serial communication off during calculation.
  You can imagine what that will be on a Teensy 4.1 @ 600 MHz - I'll get my hands on one and test.
  Won't be bad at all in my opinion!

  TODO:

  Implement code to transfer between angles and motor steps - preferably either avoiding floating point to the last step,
  or using tricks to convert floating point numbers to steps AND BACK PREDICTABLY

  After initial check, convert joint angles acquired from IK calculations to stepper steps,
  and then convert steps actually traveled back into joint angle calculations and then run FK on those angles and compare
  the target to FINAL calculated FK result which would factor in real life stepper resolution accounted for.

  Test max step rate

*/
#include <math.h>

#define PI 3.1415926535897932384626433832795

#define MAX_ALLOWED_LINEAR_ERROR 0.1 // in mm
#define MAX_ALLOWED_ANGLE_ERROR 0.1 // in degrees

const int numMotors = 6; // It won't work with anything else haha
/*
  // Robot geometry - 6 x 4 matrix
  // Denavit-Hartenberg parameters (need to confirm modified or original)
  // Later used as input in ForwardK and InverseK functions

  d - The distance between the two joints (d)
  theta - The angle between the x-axis of the first link and the common normal of the two joints (theta)
  a - The distance between the common normal and the x-axis of the second link (a)
  alpha - The angle between the z-axis of the first link and the z-axis of the second link (alpha)
*/

// For axis (joint):         1      2      3     4     5    6
const double DH_D[6] =     {133.0,  0.0,   0.0, 117.5,  0.0, 28.0}; // d parameter - distance between joints
const double DH_theta[6] = { 0.0, -90.0,   0.0,   0.0,  0.0, 0.0}; // theta parameter
const double DH_A[6] =     { 47.0, 110.0,  26.0,  0.0,  0.0, 0.0}; // "a" parameter - something like an offset of the joint compared to previous one
const double DH_alpha[6] = { -90.0,  0.0, -90.0, 90.0, -90.0, 0.0}; // alpha parameter


// Travel limits (soft limits) in steps
const double maxTravelLimits[numMotors] = {  180.0,  180.0,  180.0,  180.0,  180.0,  180.0};
const double minTravelLimits[numMotors] = { -180.0, -180.0, -180.0, -180.00, -180.0, -180.0};


// ------------ Settings ended, only internal variables ahead ------------

// Currrent joint positions, in degrees
double currentPositions[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

// ABSOLUTELY NEED A CHECK TO SEE IF THIS POSITION IS REALISTIC IN TERMS OF INVERSE AND FORWARD KINEMATICS!
// Define home position here, as it needs to be initialized early in code
const double Xhome[6] = {164.5, 0.0, 241.0, 90.0, 180.0, -90.0}; //{x, y, z, ZYZ Euler angles}
// Declaration of arrays of doubles as outputs for IK math
double Jhome[6];


// Number of predefined travel positions
const int numPoints = 6;

// Predefined positions in Cartesian coordinates for testing

// Inputs between -180 and +180 degrees
// Input of ZYZ-Y of 180.0 causes singularity and unpredictable ZYZ-Z1 and ZYZ-Z2 outputs after IK and back to FK calculation
// Surprisingly, cartesian X, Y, Z values all get calculated back and forth (cartesian - IK - FK) correctly
double X[numPoints][6] = {
  {164.5, 0.0, 141.0, 90.0, 180.0, -89.9},
  {164.5, 0.0, 141.0, 89.0, 179.0, -89.0},
  {179.2, 35.4, 141.0, 89.0, 179.0, -90.0}, // 3
  {164.5 + 50.0, 50.0, 141.0, 30.0, 179.9, -90.0}, // 4
  {164.5 + 85.3, 35.4, 141.0, -30.0, 279.9, -60.0}, // 5
  {200.5, 0.0, 141.0, 90.0, -220.0, -90.0} // 6

}; // 2D array of 14 rows and 6 columns



void setup()
{
  Serial.begin(115200);
  delay(10);

  if (numMotors != 6) {
    Serial.println("IK won't work - numMotors not 6");
    while (1);
  }

  // Repeat these settings for 6 axes
  for (int i = 0; i < numMotors; i++) {


    // Manually set position in degrees after homing - may be a very, very wrong decision
    currentPositions[i] = Xhome[i];
  }
}

void loop()
{
  bool ik_error = 0; // Flag if IK calculations failed
  double err[6] = {0, 0, 0, 0, 0, 0}; // Calculated difference between commanded and calculated position
  double maxLinErr = 0.0;
  double maxAngErr = 0.0;

  //-----------Business-------------------
  for (int j = 0; j < numPoints; j++) { // repeat for the defined number of points
    ik_error = 0;
    Serial.println();
    Serial.println(j + 1); // Data point number

    // For the last 3 values (angles), silently correct them to within +- 180 degrees from zero.
    // This will work for multiple rotations (e.g. 270 -> -90,
    // Dunno, maybe it's a bad idea to overwrite input variable...
    for (int p = 3; p < 6; p++) {
      while (X[j][p] > 180.0) {
        X[j][p] = X[j][p] - 360.0;
      }
      while (X[j][p] < -180.0) {
        X[j][p] = X[j][p] + 360.0;
      }
    } // for last 3 values

    Serial.print("IN: ");

    // create a local temporary copy of the input, because the sneaky InverseK function
    //  modifies the original input values it's being given! (Go figure! ARRGH!!!)
    double X_CPY[6];

    for (int i = 0; i < 6; i++) {

      X_CPY[i] = X[j][i];

      // Add or substract a *little bit* if the angle values are problematic (exactly equal to 90 degree increments froom 0.00)
      // Needed to prevent incorrect calculations and (at times) avoid singularity
      // By some reason 0.01 works better than both 0.1 and 0.01, giving lower errors
      if (i >= 3 && ( X_CPY[i] == 0.0 || X_CPY[i] == 90.0 || X_CPY[i] == 180.0 )) X_CPY[i] = X_CPY[i] - 0.01;
      else if (i >= 3 && ( X_CPY[i] == -90.0 || X_CPY[i] == -180.0 )) X_CPY[i] = X_CPY[i] + 0.01;

      Serial.print( X_CPY[i], 7);
      Serial.print(", ");
    }
    Serial.println();

    long timeStart = micros();

    //  array of 6 joint angle values - new one for each datapoint - again, local, temporary
    double JA[6];

    // IK calculations before moves
    // InverseK(input array of XYZ ZYZ-Euler, output array of 6 angles) - IT MODOFIES BOTH ARRAYS!
    InverseK(X_CPY, JA); // IT MODIFIES BOTH ARGUMENTS!

    Serial.print("IK: ");
    // Check output for sanity - if an impossible position is commanded the IK math returns nan
    for (int i = 0; i < 6; i++) {
      Serial.print(JA[i], 7);
      Serial.print(", ");
      if (isnan(JA[i]) || isinf(JA[i])) {
        ik_error = 1;
      }
      // Check axis travel limits: if IK calculations returned values outside of travel limts, they can not be reached anyway
      if (JA[i] > maxTravelLimits[i]) {
        ik_error = 1;
        Serial.print("IK: ");
        Serial.println("OUT_OF_BOUNDS");
      } else if (JA[i] < minTravelLimits[i]) {
        ik_error = 1;
        Serial.print("IK: ");
        Serial.println("OUT_OF_BOUNDS");
      }
    } // repeat 6 times

    Serial.println();

    // Perform reverse calculation and output. It should match initial values closely.
    double FK[6]; // Local, temporary array

    ForwardK(JA, FK);

    long timeFinish = micros() - timeStart;
    Serial.print("FK: ");

    for (int i = 0; i < 6; i++) {
      Serial.print(FK[i], 7);
      Serial.print(", ");
      // Check forward kinematics output for sanity - if it returns an insane value, there might have been a math error
      if (isnan(FK[i]) || isinf(FK[i])) {
        ik_error = 1;
      }
    }
    Serial.println();
    Serial.print("ERR: ");
    for (int i = 0; i < 6; i++) {
      err[i] = abs(FK[i] - X[j][i]); // Error from round trip calculation
      Serial.print(err[i], 7);
      Serial.print(", ");
    }
    Serial.println();

    // Normally an elevated, but not too high mismatch means we are very close to problematic values (0 / 90 / 180 deg)
    // If the mismatch is way too high, more than, say, 1mm or 1 degree, this most likely means the position is unreachable.
    maxLinErr = max(max(err[0], err[1]), err[2]); // Maximum error value for linear output
    maxAngErr = max(max(err[3], err[4]), err[5]); // Maximum error value for angle output

    Serial.print("Linear error: ");
    Serial.println(maxLinErr, 7);
    Serial.print("Angle error: ");
    Serial.println(maxAngErr, 7);

    Serial.print("Finished in ");
    Serial.print(timeFinish);
    Serial.print(" us");
    Serial.println();

    if (ik_error) Serial.println("IK ERROR!");
    if (maxLinErr > MAX_ALLOWED_LINEAR_ERROR || maxAngErr > MAX_ALLOWED_ANGLE_ERROR) Serial.println("High mismatch!");

  // Done! We have a result with output angles, which have been checked for correctness and are ready to use in other code.
  // What to do in case of error... Go figure! Maybe throw an error

  } // Repeat for specified number of data points

  // End program
  while (1);
} // loop





// ------------------------------------------------------------------------------------------------------------------
// ------------ DO NOT EDIT - Only Inverse Kinematics calculations from here below. Do not mess with them.-----------
// ------------------------------------------------------------------------------------------------------------------

void InverseK(double * Xik, double * Jik) {
  // inverse kinematics
  // input: Xik - pos value for the calculation of the inverse kinematics
  // output: Jfk - joints value for the calculation of the inversed kinematics

  // from deg to rad for angle axis inputs
  // Xik(4:6)=Xik(4:6)*pi/180;
  Xik[3] = Xik[3] * PI / 180.0;
  Xik[4] = Xik[4] * PI / 180.0;
  Xik[5] = Xik[5] * PI / 180.0;

  // Denavit-Hartenberg matrix
  double theta[6] = {DH_theta[0], DH_theta[1], DH_theta[2], DH_theta[3], DH_theta[4], DH_theta[5] }; // theta=[0; -90+0; 0; 0; 0; 0];
  double alfa[6] =  {DH_alpha[0], DH_alpha[1], DH_alpha[2], DH_alpha[3], DH_alpha[4], DH_alpha[5]}; // alfa=[-90; 0; -90; 90; -90; 0];
  double a_val[6] =     {DH_A[0], DH_A[1], DH_A[2], DH_A[3], DH_A[4], DH_A[5]}; // r=[47; 110; 26; 0; 0; 0]; // Skyentific, why didn't you include all DH parameters? What if someone has a different device than yours?
  double d_val[6] =     {DH_D[0], DH_D[1], DH_D[2], DH_D[3], DH_D[4], DH_D[5]}; // d=[133; 0; 7; 117.5; 0; 28];

  // from deg to rad
  MatrixScale(theta, 6, 1, PI / 180.0); // theta=theta*pi/180;
  MatrixScale(alfa, 6, 1, PI / 180.0); // alfa=alfa*pi/180;

  // work frame
  double Xwf[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // Xwf=[0; 0; 0; 0; 0; 0];

  // tool frame
  double Xtf[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // Xtf=[0; 0; 0; 0; 0; 0];

  // work frame transformation matrix
  double Twf[16];
  pos2tran(Xwf, Twf); // Twf=pos2tran(Xwf);

  // tool frame transformation matrix
  double Ttf[16];
  pos2tran(Xtf, Ttf); // Ttf=pos2tran(Xtf);

  // total transformation matrix
  double Twt[16];
  pos2tran(Xik, Twt); // Twt=pos2tran(Xik);

  // find T06
  double inTwf[16], inTtf[16], Tw6[16], T06[16];
  invtran(Twf, inTwf); // inTwf=invtran(Twf);
  invtran(Ttf, inTtf); // inTtf=invtran(Ttf);
  MatrixMultiply(Twt, inTtf, 4, 4, 4, Tw6); // Tw6=Twt*inTtf;
  MatrixMultiply(inTwf, Tw6, 4, 4, 4, T06); // T06=inTwf*Tw6;

  // positon of the spherical wrist
  double Xsw[3];
  // Xsw=T06(1:3,4)-d(6)*T06(1:3,3);
  Xsw[0] = T06[0 * 4 + 3] - d_val[5] * T06[0 * 4 + 2];
  Xsw[1] = T06[1 * 4 + 3] - d_val[5] * T06[1 * 4 + 2];
  Xsw[2] = T06[2 * 4 + 3] - d_val[5] * T06[2 * 4 + 2];

  // joints variable
  // Jik=zeros(6,1);
  // first joint
  Jik[0] = atan2(Xsw[1], Xsw[0]) - atan2(d_val[2], sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2])); // Jik(1)=atan2(Xsw(2),Xsw(1))-atan2(d(3),sqrt(Xsw(1)^2+Xsw(2)^2-d(3)^2));
  // second joint
  Jik[1] = PI / 2.0
           - acos((a_val[1] * a_val[1] + (Xsw[2] - d_val[0]) * (Xsw[2] - d_val[0]) + (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0]) * (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0]) - (a_val[2] * a_val[2] + d_val[3] * d_val[3])) / (2.0 * a_val[1] * sqrt((Xsw[2] - d_val[0]) * (Xsw[2] - d_val[0]) + (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0]) * (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0]))))
           - atan((Xsw[2] - d_val[0]) / (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0])); // Jik(2)=pi/2-acos((r(2)^2+(Xsw(3)-d(1))^2+(sqrt(Xsw(1)^2+Xsw(2)^2-d(3)^2)-r(1))^2-(r(3)^2+d(4)^2))/(2*r(2)*sqrt((Xsw(3)-d(1))^2+(sqrt(Xsw(1)^2+Xsw(2)^2-d(3)^2)-r(1))^2)))-atan((Xsw(3)-d(1))/(sqrt(Xsw(1)^2+Xsw(2)^2-d(3)^2)-r(1)));
  // third joint
  Jik[2] = PI
           - acos((a_val[1] * a_val[1] + a_val[2] * a_val[2] + d_val[3] * d_val[3] - (Xsw[2] - d_val[0]) * (Xsw[2] - d_val[0]) - (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0]) * (sqrt(Xsw[0] * Xsw[0] + Xsw[1] * Xsw[1] - d_val[2] * d_val[2]) - a_val[0])) / (2 * a_val[1] * sqrt(a_val[2] * a_val[2] + d_val[3] * d_val[3])))
           - atan(d_val[3] / a_val[2]); // Jik(3)=pi-acos((r(2)^2+r(3)^2+d(4)^2-(Xsw(3)-d(1))^2-(sqrt(Xsw(1)^2+Xsw(2)^2-d(3)^2)-r(1))^2)/(2*r(2)*sqrt(r(3)^2+d(4)^2)))-atan(d(4)/r(3));
  // last three joints
  double T01[16], T12[16], T23[16], T02[16], T03[16], inT03[16], T36[16];
  DH1line(theta[0] + Jik[0], alfa[0], a_val[0], d_val[0], T01); // T01=DH1line(theta(1)+Jik(1),alfa(1),r(1),d(1));
  DH1line(theta[1] + Jik[1], alfa[1], a_val[1], d_val[1], T12); // T12=DH1line(theta(2)+Jik(2),alfa(2),r(2),d(2));
  DH1line(theta[2] + Jik[2], alfa[2], a_val[2], d_val[2], T23); // T23=DH1line(theta(3)+Jik(3),alfa(3),r(3),d(3));
  MatrixMultiply(T01, T12, 4, 4, 4, T02); // T02=T01*T12;
  MatrixMultiply(T02, T23, 4, 4, 4, T03); // T03=T02*T23;
  invtran(T03, inT03); // inT03=invtran(T03);
  MatrixMultiply(inT03, T06, 4, 4, 4, T36); // T36=inT03*T06;
  // forth joint
  Jik[3] = atan2(-T36[1 * 4 + 2], -T36[0 * 4 + 2]); // Jik(4)=atan2(-T36(2,3),-T36(1,3));
  // fifth joint
  Jik[4] = atan2(sqrt(T36[0 * 4 + 2] * T36[0 * 4 + 2] + T36[1 * 4 + 2] * T36[1 * 4 + 2]), T36[2 * 4 + 2]); // Jik(5)=atan2(sqrt(T36(1,3)^2+T36(2,3)^2),T36(3,3));
  // sixth joints
  Jik[5] = atan2(-T36[2 * 4 + 1], T36[2 * 4 + 0]); // Jik(6)=atan2(-T36(3,2),T36(3,1));
  // rad to deg
  MatrixScale(Jik, 6, 1, 180.0 / PI); // Jik=Jik/pi*180;
}

void ForwardK(double * Jfk, double * Xfk)
{
  // forward kinematics
  // input: Jfk - joints value for the calculation of the forward kinematics
  // output: Xfk - pos value for the calculation of the forward kinematics

  // Denavit-Hartenberg matrix
  double theta0[6] = {DH_theta[0], DH_theta[1], DH_theta[2], DH_theta[3], DH_theta[4], DH_theta[5] }; // theta=[0; -90+0; 0; 0; 0; 0];
  double alfa[6] =  {DH_alpha[0], DH_alpha[1], DH_alpha[2], DH_alpha[3], DH_alpha[4], DH_alpha[5]}; // alfa=[-90; 0; -90; 90; -90; 0];
  double a_val[6] =     {DH_A[0], DH_A[1], DH_A[2], DH_A[3], DH_A[4], DH_A[5]}; // r=[47; 110; 26; 0; 0; 0]; // Skyentific, why didn't you include all DH parameters? What if someone has a different device than yours?
  double d_val[6] =     {DH_D[0], DH_D[1], DH_D[2], DH_D[3], DH_D[4], DH_D[5]}; // d=[133; 0; 7; 117.5; 0; 28];

  double theta[6];
  // Correct input angles by configured arm values
  // TEST: what if we don't correct the input?
  //  MatrixCopy( Jfk, 6, 1, theta); // theta=[Jfk(1); -90+Jfk(2); Jfk(3); Jfk(4); Jfk(5); Jfk(6)]; // Add input joint angles to theta from machine definition
  MatrixAdd(theta0, Jfk, 6, 1, theta); // theta=[Jfk(1); -90+Jfk(2); Jfk(3); Jfk(4); Jfk(5); Jfk(6)]; // Add input joint angles to theta from machine definition

  // from deg to rad
  MatrixScale(theta, 6, 1, PI / 180.0); // theta=theta*pi/180;
  MatrixScale(alfa, 6, 1, PI / 180.0); // alfa=alfa*pi/180;

  // work frame
  double Xwf[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // Xwf=[0; 0; 0; 0; 0; 0];

  // tool frame
  double Xtf[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0}; // Xtf=[0; 0; 0; 0; 0; 0];

  // work frame transformation matrix
  double Twf[16];
  pos2tran(Xwf, Twf); // Twf=pos2tran(Xwf);

  // tool frame transformation matrix
  double Ttf[16];
  pos2tran(Xtf, Ttf); // Ttf=pos2tran(Xtf);

  // DH homogeneous transformation matrix
  double T01[16], T12[16], T23[16], T34[16], T45[16], T56[16];
  DH1line(theta[0], alfa[0], a_val[0], d_val[0], T01); // T01=DH1line(theta(1),alfa(1),r(1),d(1));
  DH1line(theta[1], alfa[1], a_val[1], d_val[1], T12); // T12=DH1line(theta(2),alfa(2),r(2),d(2));
  DH1line(theta[2], alfa[2], a_val[2], d_val[2], T23); // T23=DH1line(theta(3),alfa(3),r(3),d(3));
  DH1line(theta[3], alfa[3], a_val[3], d_val[3], T34); // T34=DH1line(theta(4),alfa(4),r(4),d(4));
  DH1line(theta[4], alfa[4], a_val[4], d_val[4], T45); // T45=DH1line(theta(5),alfa(5),r(5),d(5));
  DH1line(theta[5], alfa[5], a_val[5], d_val[5], T56); // T56=DH1line(theta(6),alfa(6),r(6),d(6));

  double Tw1[16], Tw2[16], Tw3[16], Tw4[16], Tw5[16], Tw6[16], Twt[16];
  MatrixMultiply(Twf, T01, 4, 4, 4, Tw1); // Tw1=Twf*T01;
  MatrixMultiply(Tw1, T12, 4, 4, 4, Tw2); // Tw2=Tw1*T12;
  MatrixMultiply(Tw2, T23, 4, 4, 4, Tw3); // Tw3=Tw2*T23;
  MatrixMultiply(Tw3, T34, 4, 4, 4, Tw4); // Tw4=Tw3*T34;
  MatrixMultiply(Tw4, T45, 4, 4, 4, Tw5); // Tw5=Tw4*T45;
  MatrixMultiply(Tw5, T56, 4, 4, 4, Tw6); // Tw6=Tw5*T56;
  MatrixMultiply(Tw6, Ttf, 4, 4, 4, Twt); // Twt=Tw6*Ttf;

  // calculate pos from transformation matrix
  tran2pos(Twt, Xfk);
  // Xfk(4:6)=Xfk(4:6)/pi*180;
  /* Interestingly, Skyentific had an error here - simple wrong mathematical
      operations order. Corrected. Makes me think the math was ported from
      some different programming language that would calculate differently than C++
  */
  Xfk[3] = Xfk[3] * 180.0 / PI;
  Xfk[4] = Xfk[4] * 180.0 / PI;
  Xfk[5] = Xfk[5] * 180.0 / PI;
}

void invtran(double * Titi, double * Titf)
{
  // finding the inverse of the homogeneous transformation matrix
  // first row
  Titf[0 * 4 + 0] = Titi[0 * 4 + 0];
  Titf[0 * 4 + 1] = Titi[1 * 4 + 0];
  Titf[0 * 4 + 2] = Titi[2 * 4 + 0];
  Titf[0 * 4 + 3] = -Titi[0 * 4 + 0] * Titi[0 * 4 + 3] - Titi[1 * 4 + 0] * Titi[1 * 4 + 3] - Titi[2 * 4 + 0] * Titi[2 * 4 + 3];
  // second row
  Titf[1 * 4 + 0] = Titi[0 * 4 + 1];
  Titf[1 * 4 + 1] = Titi[1 * 4 + 1];
  Titf[1 * 4 + 2] = Titi[2 * 4 + 1];
  Titf[1 * 4 + 3] = -Titi[0 * 4 + 1] * Titi[0 * 4 + 3] - Titi[1 * 4 + 1] * Titi[1 * 4 + 3] - Titi[2 * 4 + 1] * Titi[2 * 4 + 3];
  // third row
  Titf[2 * 4 + 0] = Titi[0 * 4 + 2];
  Titf[2 * 4 + 1] = Titi[1 * 4 + 2];
  Titf[2 * 4 + 2] = Titi[2 * 4 + 2];
  Titf[2 * 4 + 3] = -Titi[0 * 4 + 2] * Titi[0 * 4 + 3] - Titi[1 * 4 + 2] * Titi[1 * 4 + 3] - Titi[2 * 4 + 2] * Titi[2 * 4 + 3];
  // forth row
  Titf[3 * 4 + 0] = 0.0;
  Titf[3 * 4 + 1] = 0.0;
  Titf[3 * 4 + 2] = 0.0;
  Titf[3 * 4 + 3] = 1.0;
}

void tran2pos(double * Ttp, double * Xtp)
{
  // pos from homogeneous transformation matrix
  Xtp[0] = Ttp[0 * 4 + 3];
  Xtp[1] = Ttp[1 * 4 + 3];
  Xtp[2] = Ttp[2 * 4 + 3];
  Xtp[4] = atan2(sqrt(Ttp[2 * 4 + 0] * Ttp[2 * 4 + 0] + Ttp[2 * 4 + 1] * Ttp[2 * 4 + 1]), Ttp[2 * 4 + 2]);
  Xtp[3] = atan2(Ttp[1 * 4 + 2] / sin(Xtp[4]), Ttp[0 * 4 + 2] / sin(Xtp[4]));
  Xtp[5] = atan2(Ttp[2 * 4 + 1] / sin(Xtp[4]), -Ttp[2 * 4 + 0] / sin(Xtp[4]));
}

void pos2tran(double * Xpt, double * Tpt)
{
  // pos to homogeneous transformation matrix
  // first row
  Tpt[0 * 4 + 0] = cos(Xpt[3]) * cos(Xpt[4]) * cos(Xpt[5]) - sin(Xpt[3]) * sin(Xpt[5]);
  Tpt[0 * 4 + 1] = -cos(Xpt[3]) * cos(Xpt[4]) * sin(Xpt[5]) - sin(Xpt[3]) * cos(Xpt[5]);
  Tpt[0 * 4 + 2] = cos(Xpt[3]) * sin(Xpt[4]);
  Tpt[0 * 4 + 3] = Xpt[0];
  // second row
  Tpt[1 * 4 + 0] = sin(Xpt[3]) * cos(Xpt[4]) * cos(Xpt[5]) + cos(Xpt[3]) * sin(Xpt[5]);
  Tpt[1 * 4 + 1] = -sin(Xpt[3]) * cos(Xpt[4]) * sin(Xpt[5]) + cos(Xpt[3]) * cos(Xpt[5]);
  Tpt[1 * 4 + 2] = sin(Xpt[3]) * sin(Xpt[4]);
  Tpt[1 * 4 + 3] = Xpt[1];
  // third row
  Tpt[2 * 4 + 0] = -sin(Xpt[4]) * cos(Xpt[5]);
  Tpt[2 * 4 + 1] = sin(Xpt[4]) * sin(Xpt[5]);
  Tpt[2 * 4 + 2] = cos(Xpt[4]);
  Tpt[2 * 4 + 3] = Xpt[2];
  // forth row
  Tpt[3 * 4 + 0] = 0.0;
  Tpt[3 * 4 + 1] = 0.0;
  Tpt[3 * 4 + 2] = 0.0;
  Tpt[3 * 4 + 3] = 1.0;
}

void DH1line(double thetadh, double alfadh, double rdh, double ddh, double * Tdh)
{
  // creats Denavit-Hartenberg homogeneous transformation matrix
  // first row
  Tdh[0 * 4 + 0] = cos(thetadh);
  Tdh[0 * 4 + 1] = -sin(thetadh) * cos(alfadh);
  Tdh[0 * 4 + 2] = sin(thetadh) * sin(alfadh);
  Tdh[0 * 4 + 3] = rdh * cos(thetadh);
  // second row
  Tdh[1 * 4 + 0] = sin(thetadh);
  Tdh[1 * 4 + 1] = cos(thetadh) * cos(alfadh);
  Tdh[1 * 4 + 2] = -cos(thetadh) * sin(alfadh);
  Tdh[1 * 4 + 3] = rdh * sin(thetadh);
  // third row
  Tdh[2 * 4 + 0] = 0.0;
  Tdh[2 * 4 + 1] = sin(alfadh);
  Tdh[2 * 4 + 2] = cos(alfadh);
  Tdh[2 * 4 + 3] = ddh;
  // forth row
  Tdh[3 * 4 + 0] = 0.0;
  Tdh[3 * 4 + 1] = 0.0;
  Tdh[3 * 4 + 2] = 0.0;
  Tdh[3 * 4 + 3] = 1.0;
}

void MatrixPrint(double * A, int m, int n, String label)
{
  // A = input matrix (m x n)
  int i, j;
  Serial.println();
  Serial.println(label);
  for (i = 0; i < m; i++)
  {
    for (j = 0; j < n; j++)
    {
      Serial.print(A[n * i + j]);
      Serial.print("\t");
    }
    Serial.println();
  }
}

void MatrixCopy(double * A, int n, int m, double * B)
{
  int i, j;
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
    {
      B[n * i + j] = A[n * i + j];
    }
}

//Matrix Multiplication Routine
// C = A*B
void MatrixMultiply(double * A, double * B, int m, int p, int n, double * C)
{
  // A = input matrix (m x p)
  // B = input matrix (p x n)
  // m = number of rows in A
  // p = number of columns in A = number of rows in B
  // n = number of columns in B
  // C = output matrix = A*B (m x n)
  int i, j, k;
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
    {
      C[n * i + j] = 0;
      for (k = 0; k < p; k++)
        C[n * i + j] = C[n * i + j] + A[p * i + k] * B[n * k + j];
    }
}


//Matrix Addition Routine
void MatrixAdd(double * A, double * B, int m, int n, double * C)
{
  // A = input matrix (m x n)
  // B = input matrix (m x n)
  // m = number of rows in A = number of rows in B
  // n = number of columns in A = number of columns in B
  // C = output matrix = A+B (m x n)
  int i, j;
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
      C[n * i + j] = A[n * i + j] + B[n * i + j];
}


//Matrix Subtraction Routine
void MatrixSubtract(double * A, double * B, int m, int n, double * C)
{
  // A = input matrix (m x n)
  // B = input matrix (m x n)
  // m = number of rows in A = number of rows in B
  // n = number of columns in A = number of columns in B
  // C = output matrix = A-B (m x n)
  int i, j;
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
      C[n * i + j] = A[n * i + j] - B[n * i + j];
}


//Matrix Transpose Routine
void MatrixTranspose(double * A, int m, int n, double * C)
{
  // A = input matrix (m x n)
  // m = number of rows in A
  // n = number of columns in A
  // C = output matrix = the transpose of A (n x m)
  int i, j;
  for (i = 0; i < m; i++)
    for (j = 0; j < n; j++)
      C[m * j + i] = A[n * i + j];
}

void MatrixScale(double * A, int m, int n, double k)
{
  for (int i = 0; i < m; i++)
    for (int j = 0; j < n; j++)
      A[n * i + j] = A[n * i + j] * k;
}
