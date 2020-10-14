/// -*- tab-width: 4; Mode: C++; c-basic-offset: 4; indent-tabs-mode: nil -*-

#define THISFIRMWARE "ArduRocket V0.2"


////////////////////////////////////////////////////////////////////////////////
// Header includes
////////////////////////////////////////////////////////////////////////////////

// AVR runtime
#include <avr/io.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <math.h>

// Libraries
#include <FastSerial.h>
#include <AP_Common.h>
#include <Arduino_Mega_ISR_Registry.h>
#include <APM_RC.h>         // ArduPilot Mega RC Library
#include <AP_GPS.h>         // ArduPilot GPS library
#include <I2C.h>			// Wayne Truchsess I2C lib
#include <SPI.h>			// Arduino SPI lib
#include <DataFlash.h>      // ArduPilot Mega Flash Memory Library
#include <AP_ADC.h>         // ArduPilot Mega Analog to Digital Converter Library
#include <AP_AnalogSource.h>// ArduPilot Mega polymorphic analog getter
#include <AP_PeriodicProcess.h> // ArduPilot Mega TimerProcess
#include <AP_Baro.h>        // ArduPilot barometer library
#include <AP_Compass.h>     // ArduPilot Mega Magnetometer Library
#include <AP_Math.h>        // ArduPilot Mega Vector/Matrix math Library
#include <AP_InertialSensor.h> // Inertial Sensor (uncalibrated IMU) Library
#include <AP_IMU.h>         // ArduPilot Mega IMU Library
#include <AP_AHRS.h>         // ArduPilot Mega DCM Library
#include <PID.h>            // PID library
#include <RC_Channel.h>     // RC Channel Library
#include <AP_RangeFinder.h>	// Range finder library
#include <Filter.h>			// Filter library
#include <ModeFilter.h>		// Mode Filter from Filter library
#include <LowPassFilter.h>	// LowPassFilter class (inherits from Filter class)
#include <AP_Relay.h>       // APM relay
#include <AP_Camera.h>		// Photo or video camera
#include <AP_Airspeed.h>
#include <memcheck.h>

// Configuration
#include "config.h"

#include <GCS_MAVLink.h>    // MAVLink GCS definitions

#include <AP_Mount.h>		// Camera/Antenna mount

// Local modules
#include "defines.h"
#include "Parameters.h"
#include "GCS.h"

#include <AP_Declination.h> // ArduPilot Mega Declination Helper Library

////////////////////////////////////////////////////////////////////////////////
// Serial ports
////////////////////////////////////////////////////////////////////////////////
//
// Note that FastSerial port buffers are allocated at ::begin time,
// so there is not much of a penalty to defining ports that we don't
// use.
//
FastSerialPort0(Serial);        // FTDI/console
FastSerialPort1(Serial1);       // GPS port
#if TELEMETRY_UART2 == ENABLED
 // solder bridge set to enable UART2 instead of USB MUX
 FastSerialPort2(Serial3);
#else
 FastSerialPort3(Serial3);       // Telemetry port for APM1
#endif

////////////////////////////////////////////////////////////////////////////////
// ISR Registry
////////////////////////////////////////////////////////////////////////////////
Arduino_Mega_ISR_Registry isr_registry;


////////////////////////////////////////////////////////////////////////////////
// APM_RC_Class Instance
////////////////////////////////////////////////////////////////////////////////
#if CONFIG_APM_HARDWARE == APM_HARDWARE_APM2
    APM_RC_APM2 APM_RC;
#else
    APM_RC_APM1 APM_RC;
#endif

////////////////////////////////////////////////////////////////////////////////
// Dataflash
////////////////////////////////////////////////////////////////////////////////
#if CONFIG_APM_HARDWARE == APM_HARDWARE_APM2
    DataFlash_APM2 DataFlash;
#else
    DataFlash_APM1   DataFlash;
#endif


////////////////////////////////////////////////////////////////////////////////
// Parameters
////////////////////////////////////////////////////////////////////////////////
//
// Global parameters are all contained within the 'g' class.
//
static Parameters      g;


////////////////////////////////////////////////////////////////////////////////
// prototypes
// static void update_events(void);


////////////////////////////////////////////////////////////////////////////////
// Sensors
////////////////////////////////////////////////////////////////////////////////
//
// There are three basic options related to flight sensor selection.
//
// - Normal flight mode.  Real sensors are used.
// - HIL Attitude mode.  Most sensors are disabled, as the HIL
//   protocol supplies attitude information directly.
// - HIL Sensors mode.  Synthetic sensors are configured that
//   supply data from the simulation.
//

// All GPS access should be through this pointer.
static GPS         *g_gps;

// flight modes convenience array
static AP_Int8		*flight_modes = &g.flight_mode1;

#if HIL_MODE == HIL_MODE_DISABLED

// real sensors
#if CONFIG_ADC == ENABLED
static AP_ADC_ADS7844          adc;
#endif

#ifdef DESKTOP_BUILD
AP_Baro_BMP085_HIL      barometer;
AP_Compass_HIL          compass;
#include <SITL.h>
SITL					sitl;
#else

#if CONFIG_BARO == AP_BARO_BMP085
# if CONFIG_APM_HARDWARE == APM_HARDWARE_APM2
static AP_Baro_BMP085          barometer(true);
# else
static AP_Baro_BMP085          barometer(false);
# endif
#elif CONFIG_BARO == AP_BARO_MS5611
static AP_Baro_MS5611          barometer;
#endif

static AP_Compass_HMC5843      compass;
#endif

// real GPS selection
#if   GPS_PROTOCOL == GPS_PROTOCOL_AUTO
AP_GPS_Auto     g_gps_driver(&Serial1, &g_gps);

#elif GPS_PROTOCOL == GPS_PROTOCOL_NMEA
AP_GPS_NMEA     g_gps_driver(&Serial1);

#elif GPS_PROTOCOL == GPS_PROTOCOL_SIRF
AP_GPS_SIRF     g_gps_driver(&Serial1);

#elif GPS_PROTOCOL == GPS_PROTOCOL_UBLOX
AP_GPS_UBLOX    g_gps_driver(&Serial1);

#elif GPS_PROTOCOL == GPS_PROTOCOL_MTK
AP_GPS_MTK      g_gps_driver(&Serial1);

#elif GPS_PROTOCOL == GPS_PROTOCOL_MTK16
AP_GPS_MTK16    g_gps_driver(&Serial1);

#elif GPS_PROTOCOL == GPS_PROTOCOL_NONE
AP_GPS_None     g_gps_driver(NULL);

#else
 #error Unrecognised GPS_PROTOCOL setting.
#endif // GPS PROTOCOL

# if CONFIG_IMU_TYPE == CONFIG_IMU_MPU6000
  AP_InertialSensor_MPU6000 ins( CONFIG_MPU6000_CHIP_SELECT_PIN );
# else
  AP_InertialSensor_Oilpan ins( &adc );
#endif // CONFIG_IMU_TYPE
AP_IMU_INS imu( &ins );

#if QUATERNION_ENABLE == ENABLED
  AP_AHRS_Quaternion ahrs(&imu, g_gps);
#else
  AP_AHRS_DCM  ahrs(&imu, g_gps);
#endif

#elif HIL_MODE == HIL_MODE_SENSORS
// sensor emulators
AP_ADC_HIL              adc;
AP_Baro_BMP085_HIL      barometer;
AP_Compass_HIL          compass;
AP_GPS_HIL              g_gps_driver(NULL);
AP_InertialSensor_Oilpan ins( &adc );
AP_IMU_Shim imu;
AP_AHRS_DCM  ahrs(&imu, g_gps);

#elif HIL_MODE == HIL_MODE_ATTITUDE
AP_ADC_HIL              adc;
AP_IMU_Shim             imu; // never used
AP_AHRS_HIL             ahrs(&imu, g_gps);
AP_GPS_HIL              g_gps_driver(NULL);
AP_Compass_HIL          compass; // never used
AP_Baro_BMP085_HIL      barometer;
#ifdef DESKTOP_BUILD
#include <SITL.h>
SITL					sitl;
AP_InertialSensor_Oilpan ins( &adc );
#endif

#else
 #error Unrecognised HIL_MODE setting.
#endif // HIL MODE

// we always have a timer scheduler
AP_TimerProcess timer_scheduler;


////////////////////////////////////////////////////////////////////////////////
// GCS selection
////////////////////////////////////////////////////////////////////////////////
//
GCS_MAVLINK	gcs0;
GCS_MAVLINK	gcs3;

////////////////////////////////////////////////////////////////////////////////
// PITOT selection
////////////////////////////////////////////////////////////////////////////////
//
ModeFilterInt16_Size5 sonar_mode_filter(2);

#if CONFIG_PITOT_SOURCE == PITOT_SOURCE_ADC
AP_AnalogSource_ADC pitot_analog_source( &adc,
                        CONFIG_PITOT_SOURCE_ADC_CHANNEL, 1.0);
#elif CONFIG_PITOT_SOURCE == PITOT_SOURCE_ANALOG_PIN
AP_AnalogSource_Arduino pitot_analog_source(CONFIG_PITOT_SOURCE_ANALOG_PIN, 4.0);
#endif

#if SONAR_TYPE == MAX_SONAR_XL
	AP_RangeFinder_MaxsonarXL sonar(&pitot_analog_source, &sonar_mode_filter);
#elif SONAR_TYPE == MAX_SONAR_LV
	// XXX honestly I think these output the same values
	// If someone knows, can they confirm it?
	AP_RangeFinder_MaxsonarXL sonar(&pitot_analog_source, &sonar_mode_filter);
#endif

AP_Relay relay;



////////////////////////////////////////////////////////////////////////////////
// Global variables
////////////////////////////////////////////////////////////////////////////////

// APM2 only
#if USB_MUX_PIN > 0
static bool usb_connected;
#endif

static const char *comma = ",";

static const char* flight_mode_strings[] = {
	"Manual",		// fly rocket with RC controller
	"Stabilize",	// use canards, thrust vector, or cold gas thrusters to maintain initial attitude either under thrust or not
	"Zenith",		// like stabilize but also seek 90 degree vertical attitude
	"Trajectory"	// like stabilize but also roll to specified launch azimuth and run pitch over schedule program (for orbital/suborbital)
	};


/* Radio values
		Channel assignments
			1   Ailerons (rudder if no ailerons)
			2   Elevator
			3   Throttle
			4   Rudder (if we have ailerons)
			5   Aux5
			6   Aux6
			7   Aux7
			8   Aux8/Mode
		Each Aux channel can be configured to have any of the available auxiliary functions assigned to it.
		See libraries/RC_Channel/RC_Channel_aux.h for more information
*/

////////////////////////////////////////////////////////////////////////////////
// Radio
////////////////////////////////////////////////////////////////////////////////
// This is the state of the flight control system
// There are multiple states defined such as MANUAL, FBW-A, AUTO
byte    control_mode        = INITIALISING;
// Used to maintain the state of the previous control switch position
// This is set to -1 when we need to re-read the switch
byte 	oldSwitchPosition;
// This is used to enable the inverted flight feature
bool    inverted_flight     = false;
// These are trim values used for elevon control
// For elevons radio_in[CH_ROLL] and radio_in[CH_PITCH] are equivalent aileron and elevator, not left and right elevon
static uint16_t elevon1_trim  = 1500;
static uint16_t elevon2_trim  = 1500;
// These are used in the calculation of elevon1_trim and elevon2_trim
static uint16_t ch1_temp      = 1500;
static uint16_t ch2_temp  	= 1500;
// These are values received from the GCS if the user is using GCS joystick
// control and are substituted for the values coming from the RC radio
static int16_t rc_override[8] = {0,0,0,0,0,0,0,0};
// A flag if GCS joystick control is in use
static bool rc_override_active = false;

////////////////////////////////////////////////////////////////////////////////
// Failsafe
////////////////////////////////////////////////////////////////////////////////
// A tracking variable for type of failsafe active
// Used for failsafe based on loss of RC signal or GCS signal
static int16_t 	failsafe;
// Used to track if the value on channel 3 (throtttle) has fallen below the failsafe threshold
// RC receiver should be set up to output a low throttle value when signal is lost
static bool 	ch3_failsafe;
// A timer used to help recovery from unusual attitudes.  If we enter an unusual attitude
// while in autonomous flight this variable is used  to hold roll at 0 for a recovery period
static byte    crash_timer;
// A timer used to track how long since we have received the last GCS heartbeat or rc override message
static uint32_t rc_override_fs_timer = 0;
// A timer used to track how long we have been in a "short failsafe" condition due to loss of RC signal
static uint32_t ch3_failsafe_timer = 0;

////////////////////////////////////////////////////////////////////////////////
// LED output
////////////////////////////////////////////////////////////////////////////////
// state of the GPS light (on/off)
static bool GPS_light;

////////////////////////////////////////////////////////////////////////////////
// GPS variables
////////////////////////////////////////////////////////////////////////////////
// This is used to scale GPS values for EEPROM storage
// 10^7 times Decimal GPS means 1 == 1cm
// This approximation makes calculations integer and it's easy to read
static const 	float t7			= 10000000.0;
// We use atan2 and other trig techniques to calaculate angles
// A counter used to count down valid gps fixes to allow the gps estimate to settle
// before recording our home position (and executing a ground start if we booted with an air start)
static byte 	ground_start_count	= 5;
// Used to compute a speed estimate from the first valid gps fixes to decide if we are
// on the ground or in the air.  Used to decide if a ground start is appropriate if we
// booted with an air start.
static int16_t     ground_start_avg;
// Tracks if GPS is enabled based on statup routine
// If we do not detect GPS at startup, we stop trying and assume GPS is not connected
static bool	GPS_enabled 	= false;

////////////////////////////////////////////////////////////////////////////////
// Location & Navigation
////////////////////////////////////////////////////////////////////////////////
// Constants
const	float radius_of_earth 	= 6378100;	// meters
const	float gravity 			= 9.81;		// meters/ sec^2
// This is the currently calculated direction to fly.
// deg * 100 : 0 to 360

// There may be two active commands in Auto mode.
// This indicates the active navigation command by index number
static byte	nav_command_index;
// This indicates the active non-navigation command by index number
static byte	non_nav_command_index;
// This is the command type (eg navigate to waypoint) of the active navigation command
static byte	nav_command_ID		= NO_COMMAND;
static byte	non_nav_command_ID	= NO_COMMAND;

////////////////////////////////////////////////////////////////////////////////
// Battery Sensors
////////////////////////////////////////////////////////////////////////////////
// Battery pack 1 voltage.  Initialized above the low voltage threshold to pre-load the filter and prevent low voltage events at startup.
static float 	battery_voltage1 	= LOW_VOLTAGE * 1.05;
// Battery pack 1 instantaneous currrent draw.  Amperes
static float	current_amps1;
// Totalized current (Amp-hours) from battery 1
static float	current_total1;

////////////////////////////////////////////////////////////////////////////////
// Airspeed Sensors
////////////////////////////////////////////////////////////////////////////////
AP_Airspeed airspeed(&pitot_analog_source, AIRSPEED_RATIO, AIRSPEED_SENSOR);

////////////////////////////////////////////////////////////////////////////////
// Navigation control variables
////////////////////////////////////////////////////////////////////////////////
// The instantaneous desired bank angle.  Hundredths of a degree
static int32_t	nav_roll;
// The instantaneous desired pitch angle.  Hundredths of a degree
static int32_t	nav_pitch;
// The instantaneous desired yaw angle.  Hundredths of a degree
static int32_t	nav_yaw;

////////////////////////////////////////////////////////////////////////////////
// repeating event control
////////////////////////////////////////////////////////////////////////////////
// Flag indicating current event type
static byte 		event_id;
// when the event was started in ms
static int32_t 		event_timer;
// how long to delay the next firing of event in millis
static uint16_t 	event_delay;
// how many times to cycle : -1 (or -2) = forever, 2 = do one cycle, 4 = do two cycles
static int16_t 		event_repeat = 0;
// per command value, such as PWM for servos
static int16_t 		event_value;
// the value used to cycle events (alternate value to event_value)
static int16_t 		event_undo_value;

////////////////////////////////////////////////////////////////////////////////
// Conditional command
////////////////////////////////////////////////////////////////////////////////
// A value used in condition commands (eg delay, change alt, etc.)
// For example in a change altitude command, it is the altitude to change to.
static int32_t 	condition_value;
// A starting value used to check the status of a conditional command.
// For example in a delay command the condition_start records that start time for the delay
static uint32_t condition_start;
// A value used in condition commands.  For example the rate at which to change altitude.
static int16_t 		condition_rate;

////////////////////////////////////////////////////////////////////////////////
// 3D Location vectors
// Location structure defined in AP_Common
////////////////////////////////////////////////////////////////////////////////
// The home location used for RTL.  The location is set when we first get stable GPS lock
static struct 	Location home;
// Flag for if we have g_gps lock and have set the home location
static bool	home_is_set;
// The location of the previous waypoint.  Used for track following and altitude ramp calculations
static struct 	Location prev_WP;
// The plane's current location
static struct 	Location current_loc;
// The location of the current/active waypoint.  Used for altitude ramp, track following and loiter calculations.
static struct 	Location next_WP;
// The location of the active waypoint in Guided mode.
static struct  	Location guided_WP;
// The location structure information from the Nav command being processed
static struct 	Location next_nav_command;
// The location structure information from the Non-Nav command being processed
static struct 	Location next_nonnav_command;

////////////////////////////////////////////////////////////////////////////////
// IMU variables
////////////////////////////////////////////////////////////////////////////////
// The main loop execution time.  Seconds
//This is the time between calls to the DCM algorithm and is the Integration time for the gyros.
static float G_Dt						= 0.02;

////////////////////////////////////////////////////////////////////////////////
// Performance monitoring
////////////////////////////////////////////////////////////////////////////////
// Timer used to accrue data and trigger recording of the performanc monitoring log message
static int32_t 	perf_mon_timer;
// The maximum main loop execution time recorded in the current performance monitoring interval
static int 	G_Dt_max = 0;
// The number of gps fixes recorded in the current performance monitoring interval
static int16_t 	gps_fix_count = 0;
// A variable used by developers to track performanc metrics.
// Currently used to record the number of GCS heartbeat messages received
static int16_t		pmTest1 = 0;


////////////////////////////////////////////////////////////////////////////////
// System Timers
////////////////////////////////////////////////////////////////////////////////
// Time in miliseconds of start of main control loop.  Milliseconds
static uint32_t 	fast_loopTimer;
// Time Stamp when fast loop was complete.  Milliseconds
static uint32_t 	fast_loopTimeStamp;
// Number of milliseconds used in last main loop cycle
static uint8_t 		delta_ms_fast_loop;
// Counter of main loop executions.  Used for performance monitoring and failsafe processing
static uint16_t			mainLoop_count;

// Time in miliseconds of start of medium control loop.  Milliseconds
static uint32_t 	medium_loopTimer;
// Counters for branching from main control loop to slower loops
static byte 			medium_loopCounter;
// Number of milliseconds used in last medium loop cycle
static uint8_t			delta_ms_medium_loop;

// Counters for branching from medium control loop to slower loops
static byte 			slow_loopCounter;
// Counter to trigger execution of very low rate processes
static byte 			superslow_loopCounter;
// Counter to trigger execution of 1 Hz processes
static byte				counter_one_herz;

// % MCU cycles used
static float 			load;


// Camera/Antenna mount tracking and stabilisation stuff
// --------------------------------------
#if MOUNT == ENABLED
// current_loc uses the baro/gps soloution for altitude rather than gps only.
// mabe one could use current_loc for lat/lon too and eliminate g_gps alltogether?
AP_Mount camera_mount(&current_loc, g_gps, &ahrs);
#endif

#if CAMERA == ENABLED
//pinMode(camtrig, OUTPUT);			// these are free pins PE3(5), PH3(15), PH6(18), PB4(23), PB5(24), PL1(36), PL3(38), PA6(72), PA7(71), PK0(89), PK1(88), PK2(87), PK3(86), PK4(83), PK5(84), PK6(83), PK7(82)
#endif

////////////////////////////////////////////////////////////////////////////////
// Top-level logic
////////////////////////////////////////////////////////////////////////////////

void setup() {
	memcheck_init();
	init_ardupilot();
}

void loop()
{
	// We want this to execute at 50Hz if possible
	// -------------------------------------------
	if (millis()-fast_loopTimer > 19) {
		delta_ms_fast_loop	= millis() - fast_loopTimer;
		load                = (float)(fast_loopTimeStamp - fast_loopTimer)/delta_ms_fast_loop;
		G_Dt                = (float)delta_ms_fast_loop / 1000.f;
		fast_loopTimer      = millis();

		mainLoop_count++;

		// Execute the fast loop
		// ---------------------
		fast_loop();

		// Execute the medium loop
		// -----------------------
		medium_loop();

		counter_one_herz++;
		if(counter_one_herz == 50){
			one_second_loop();
			counter_one_herz = 0;
		}

		if (millis() - perf_mon_timer > 20000) {
			if (mainLoop_count != 0) {
				if (g.log_bitmask & MASK_LOG_PM)
					#if HIL_MODE != HIL_MODE_ATTITUDE
					Log_Write_Performance();
					#endif

				resetPerfData();
			}
		}

		fast_loopTimeStamp = millis();
	}
}

// Main loop 50Hz
static void fast_loop()
{
	// This is the fast loop - we want it to execute at 50Hz if possible
	// -----------------------------------------------------------------
	if (delta_ms_fast_loop > G_Dt_max)
		G_Dt_max = delta_ms_fast_loop;

	// Read radio
	// ----------
	read_radio();

    // try to send any deferred messages if the serial port now has
    // some space available
    gcs_send_message(MSG_RETRY_DEFERRED);

	// Read Airspeed
	// -------------
	if (airspeed.enabled()) {
#if HIL_MODE != HIL_MODE_ATTITUDE
		read_airspeed();
#endif
	}

	#if HIL_MODE == HIL_MODE_SENSORS
		// update hil before AHRS update
		gcs_update();
	#endif

    ahrs.update();

	// uses the yaw from the DCM to give more accurate turns
//	calc_bearing_error();

	#if HIL_MODE == HIL_MODE_DISABLED
		if (g.log_bitmask & MASK_LOG_ATTITUDE_FAST)
			Log_Write_Attitude(ahrs.roll_sensor, ahrs.pitch_sensor, ahrs.yaw_sensor);

		if (g.log_bitmask & MASK_LOG_RAW)
			Log_Write_Raw();
	#endif

	// inertial navigation
	// ------------------
	#if INERTIAL_NAVIGATION == ENABLED
		// TODO: implement inertial nav function
		inertialNavigation();
	#endif

	// custom code/exceptions for flight modes
	// ---------------------------------------
	update_current_flight_mode();

	// apply desired roll, pitch and yaw to the plane
	// ----------------------------------------------
	if (control_mode > MANUAL)
		stabilize();

	// write out the servo PWM values
	// ------------------------------
	set_servos();

    gcs_update();
    gcs_data_stream_send();
}

static void medium_loop()
{
#if MOUNT == ENABLED
	camera_mount.update_mount_position();
#endif

#if CAMERA == ENABLED
	g.camera.trigger_pic_cleanup();
#endif

	// This is the start of the medium (10 Hz) loop pieces
	// -----------------------------------------
	switch(medium_loopCounter) {

		// This case deals with the GPS
		//-------------------------------
		case 0:
			medium_loopCounter++;
			if(GPS_enabled){
                update_GPS();
//                calc_gndspeed_undershoot();
            }

			#if HIL_MODE != HIL_MODE_ATTITUDE
            if (g.compass_enabled && compass.read()) {
                ahrs.set_compass(&compass);
                compass.null_offsets();
            } else {
                ahrs.set_compass(NULL);
            }
			#endif
/*{
Serial.print(ahrs.roll_sensor, DEC);	Serial.printf_P(PSTR("\t"));
Serial.print(ahrs.pitch_sensor, DEC);	Serial.printf_P(PSTR("\t"));
Serial.print(ahrs.yaw_sensor, DEC);	Serial.printf_P(PSTR("\t"));
Vector3f tempaccel = imu.get_accel();
Serial.print(tempaccel.x, DEC);	Serial.printf_P(PSTR("\t"));
Serial.print(tempaccel.y, DEC);	Serial.printf_P(PSTR("\t"));
Serial.println(tempaccel.z, DEC);
}*/

			break;

		// This case performs some navigation computations
		//------------------------------------------------
		case 1:
			medium_loopCounter++;


			if(g_gps->new_data){
				g_gps->new_data 	= false;
				// calculate the plane's desired bearing
				// -------------------------------------
//				navigate();  // ArduRocket needs new navigate code
			}

			break;

		// command processing
		//------------------------------
		case 2:
			medium_loopCounter++;

			// Read altitude from sensors
			// ------------------
			update_alt();
//			if(g.sonar_enabled) sonar_alt = sonar.read();

			break;

		// This case deals with sending high rate telemetry
		//-------------------------------------------------
		case 3:
			medium_loopCounter++;

			#if HIL_MODE != HIL_MODE_ATTITUDE
				if ((g.log_bitmask & MASK_LOG_ATTITUDE_MED) && !(g.log_bitmask & MASK_LOG_ATTITUDE_FAST))
					Log_Write_Attitude(ahrs.roll_sensor, ahrs.pitch_sensor, ahrs.yaw_sensor);

				if (g.log_bitmask & MASK_LOG_CTUN)
					Log_Write_Control_Tuning();
			#endif

			if (g.log_bitmask & MASK_LOG_NTUN)
				Log_Write_Nav_Tuning();

			if (g.log_bitmask & MASK_LOG_GPS)
				Log_Write_GPS(g_gps->time, current_loc.lat, current_loc.lng, g_gps->altitude, current_loc.alt, (long) g_gps->ground_speed, g_gps->ground_course, g_gps->fix, g_gps->num_sats);
			break;

		// This case controls the slow loop
		//---------------------------------
		case 4:
			medium_loopCounter = 0;
			delta_ms_medium_loop	= millis() - medium_loopTimer;
			medium_loopTimer      	= millis();

			if (g.battery_monitoring != 0){
				read_battery();
			}

			slow_loop();
			break;
	}
}

static void slow_loop()
{
	// This is the slow (3 1/3 Hz) loop pieces
	//----------------------------------------
	switch (slow_loopCounter){
		case 0:
			slow_loopCounter++;
			superslow_loopCounter++;
			if(superslow_loopCounter >=200) {				//	200 = Execute every minute
				#if HIL_MODE != HIL_MODE_ATTITUDE
					if(g.compass_enabled) {
						compass.save_offsets();
					}
				#endif

				superslow_loopCounter = 0;
			}
			break;

		case 1:
			slow_loopCounter++;

			// Read 3-position switch on radio
			// -------------------------------
			read_control_switch();

			// Read Control Surfaces/Mix switches
			// ----------------------------------
			update_servo_switches();

#if CONFIG_APM_HARDWARE == APM_HARDWARE_APM1
			update_aux_servo_function(&g.rc_5, &g.rc_6, &g.rc_7, &g.rc_8);
#else
			update_aux_servo_function(&g.rc_5, &g.rc_6, &g.rc_7, &g.rc_8, &g.rc_9, &g.rc_10, &g.rc_11);
#endif
			break;

		case 2:
			slow_loopCounter = 0;
//			update_events();

            mavlink_system.sysid = g.sysid_this_mav;		// This is just an ugly hack to keep mavlink_system.sysid sync'd with our parameter

#if USB_MUX_PIN > 0
            check_usb_mux();
#endif

			break;
	}
}

static void one_second_loop()
{
	if (g.log_bitmask & MASK_LOG_CUR)
		Log_Write_Current();

	// send a heartbeat
	gcs_send_message(MSG_HEARTBEAT);
}

static void update_GPS(void)
{
	g_gps->update();
	update_GPS_light();

	if (g_gps->new_data && g_gps->fix) {
		// for performance
		// ---------------
		gps_fix_count++;

		if(ground_start_count > 1){
			ground_start_count--;
			ground_start_avg += g_gps->ground_speed;

		} else if (ground_start_count == 1) {
			// We countdown N number of good GPS fixes
			// so that the altitude is more accurate
			// -------------------------------------
			if (current_loc.lat == 0) {
				ground_start_count = 5;

			} else {
				if(ENABLE_AIR_START == 1 && (ground_start_avg / 5) < SPEEDFILT){
					startup_ground();

					if (g.log_bitmask & MASK_LOG_CMD)
						Log_Write_Startup(TYPE_GROUNDSTART_MSG);

//					init_home();
				} else if (ENABLE_AIR_START == 0) {
//					init_home();
				}

				if (g.compass_enabled) {
					// Set compass declination automatically
					compass.set_initial_location(g_gps->latitude, g_gps->longitude);
				}
				ground_start_count = 0;
			}
		}

		current_loc.lng = g_gps->longitude;    // Lon * 10**7
		current_loc.lat = g_gps->latitude;     // Lat * 10**7
	}
}

static void update_current_flight_mode(void)
{
// removed auto takeoff/land for ArduRocket

// removed all unnecessary modes for ArduRocket
  		switch(control_mode){
  			case STABILIZE:
  				nav_roll	= 0;
  				nav_pitch	= 0;
				nav_yaw		= 0;
  				break;

  			case MANUAL:
  				// servo_out is for Sim control only
  				// ---------------------------------
  				g.channel_roll.servo_out = g.channel_roll.pwm_to_angle();
  				g.channel_pitch.servo_out = g.channel_pitch.pwm_to_angle();
  				g.channel_yaw.servo_out = g.channel_yaw.pwm_to_angle();
  				break;
  		}
}

static void update_alt()
{
	#if HIL_MODE == HIL_MODE_ATTITUDE
		current_loc.alt = g_gps->altitude;
	#else
		// this function is in place to potentially add a sonar sensor in the future
		//altitude_sensor = BARO;

        if (barometer.healthy) {
            current_loc.alt = (1 - g.altitude_mix) * g_gps->altitude;			// alt_MSL centimeters (meters * 100)
            current_loc.alt += g.altitude_mix * (read_barometer() + home.alt);
        } else if (g_gps->fix) {
            current_loc.alt = g_gps->altitude; // alt_MSL centimeters (meters * 100)
        }
	#endif
}
