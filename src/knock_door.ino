/* Detects patterns of knocks and triggers a motor to unlock
  it if the pattern is correct.
  By Steve Hoefer http://grathio.com
  Version 0.1.10.20.10
  Licensed under Creative Commons Attribution-Noncommercial-Share Alike 3.0
  http://creativecommons.org/licenses/by-nc-sa/3.0/us/
  (In short: Do what you want, just be sure to include this line and the four above it, and don't sell it or use it in anything you sell without contacting me.)
*/

#include <Servo.h>      // Thư viện điều khiển servo

//Định nghĩa chân pin
const int knockSensor    = 0; // Cảm biến áp điện
const int programSwitch  = 2; // Nút nhấn
const int programSwitch2 = 7; // Nút nhấn
const int lockMotor      = 8; // Động cơ mở cửa
const int redLED         = 4; // LED trạng thái đỏ
const int greenLED       = 5; // LED trạng thái xanh
const int servoPin       = 8; // Chân điều khiển servo
const int hallPin        = 6; // Chân cảm biến từ trường

//Cẫu hình nhận biết
const int threshold          = 40;     // tín hiệu nhỏ nhất để xác định là một tiếng gõ
const int rejectValue        = 25;     // Tỉ lệ phần trăm khác nhau giữa khoảng thời gian một tiếng gõ, nếu lớn hơn thì không mở khóa
const int averageRejectValue = 15;     // If the average timing of the knocks is off by this percent we don't unlock.
const int knockFadeTime      = 150;    // milis dây delay trước khi lắng nghe tiếng gõ tiếp theo

const int maximumKnocks      = 20;     // Số lần gõ cửa tối đa cho một kiểu gõ
const int knockComplete      = 1200;   // Thời gian chờ lâu nhất để xác nhận gõ cửa xong

// Biến.
int secretCode[maximumKnocks] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int knockReadings[maximumKnocks];       // Khi ai đó gõ cửa, biến này sẽ lưu lại khoảng cách giữa mỗi lần gõ
int knockSensorValue          = 0;      // Lưu lại giá trị trả về từ cảm biến áp điện.
int hallValue                 = 0;      //giá trị cảm biến từ trường
int isLock                    = true;   //đã khóa cửa hay chưa
unsigned long timePressStart  = 1;
int pressTime                 = 0;

bool shouldKeepDoorOpen    = false;
bool programButtonPressed  = false; // nút nhấn cửa bên trong
bool programButtonPressed2 = false; // nút nhấn cửa bên ngoài
bool isEnableDoorGuard     = true; // trạng thái bảo vệ cửa on/off


//Cẫu hình động cơ servo
const int servoUnlock = 90;
const int servoLock = 120;
Servo myservo;

//***************************************************
//*           WIFI CONFIG                           *
//***************************************************

bool hasRequest = false;

#define STDIO_PROTOCOL_HTTP     80
#define STDIO_PROTOCOL_HTTPS    443
#define STDIO_PROTOCOL_FTP      21
#define STDIO_PROTOCOL_CURRENT  STDIO_PROTOCOL_HTTP

#define STDIO_CHAR_CR     0x0D
#define STDIO_CHAR_LF     0x0A

#define STDIO_STRING_EMPTY  ""

#define STDIO_DELAY_SEED  1000
#define STDIO_DELAY_1X    (1*STDIO_DELAY_SEED)
#define STDIO_DELAY_2X    (2*STDIO_DELAY_SEED)
#define STDIO_DELAY_3X    (3*STDIO_DELAY_SEED)
#define STDIO_DELAY_4X    (4*STDIO_DELAY_SEED)
#define STDIO_DELAY_5X    (5*STDIO_DELAY_SEED)

#define CMD_SEND_BEGIN    "AT+CIPSEND=0"
#define CMD_SEND_END      "AT+CIPCLOSE=0"

#define WIFI_NAME         "doom"
#define WIFI_PASS         "12345678"

void pressToOpen(const bool enablePressOutdoor = false);

void setup() {
	pinMode(lockMotor, OUTPUT);
	pinMode(redLED, OUTPUT);
	pinMode(greenLED, OUTPUT);
	pinMode(programSwitch, INPUT);
	pinMode(hallPin, INPUT);

	Serial.begin(115200);
	Serial.println("Program start.");

	myservo.attach(servoPin);

	triggerDoorlock();

	delay(STDIO_DELAY_5X);
	initESP8266();

	digitalWrite(greenLED, HIGH); // Để đèn xanh trong trạng thái chờ
}

void loop() {
	// lắng nghe cảm biến, nếu có tiếng gõ
	knockSensorValue = analogRead(knockSensor);

	listenEventPress(programSwitch, programButtonPressed);
	listenEventPress(programSwitch2, programButtonPressed2);

	while(Serial.available())
	{
		bufferingRequest(Serial.read());
	}

	if(hasRequest == true)
	{
		String htmlResponse = "<!doctype html>"
					"<html>"
					"<head>"
						"<title>DOOR DEMO</title>"
					"</head>"
					"<body style='text-aglin: center'>"
						"<h1>DOOR REMOTE</h1>"
						"<h3><a href='http://192.168.4.1/?DOOR=UNLOCK'>Mo Khoa Cua</a></h3>"
						"<form action='' method='GET'>"
						"<input type='radio' name='DOOR' value='GUARD_ON' /> Enable Door Guard<br/>"
						"<input type='radio' name='DOOR' value='GUARD_OFF' /> Disable Door Guard<br/>"
						"<input type='submit' value='Submit' />"
						"</form>"
					"</body>"
					"</html>";

		String beginSendCmd = String(CMD_SEND_BEGIN) + "," + htmlResponse.length();
		deliverMessage(beginSendCmd, STDIO_DELAY_1X);
		deliverMessage(htmlResponse, STDIO_DELAY_1X);
		deliverMessage(CMD_SEND_END, STDIO_DELAY_1X);
		hasRequest = false;
	}

	if (isLock == true) {
		if (isEnableDoorGuard == true){
			pressToOpen();
			listenToSecretKnock();
		} else {
			pressToOpen(true);
		}
	}

	if (isLock == false && isHall() == false) {
		shouldKeepDoorOpen = false;
	}

	if (isLock == false && isHall() && shouldKeepDoorOpen == false) {
		triggerDoorlock();
	}
}

bool isHall() {
  return digitalRead(hallPin) == LOW ? true : false;
}

void listenEventPress(const int &switchButton, bool &button) {
		if (digitalRead(switchButton) == HIGH) { // nếu đã nhấn nút
		button = true; // lưu lại sự kiện
		digitalWrite(redLED, HIGH); // và hiện đèn đỏ
	} else {
		button = false;
		digitalWrite(redLED, LOW);
	}
}

// Sự kiện khi người dùng gõ cửa
void listenToSecretKnock() {
	if (knockSensorValue <= threshold) { // nếu có gõ cửa
		//Serial.println(knockSensorValue);
		return;
	}

	Serial.println("Gõ Bắt Đầu");

	bool is_use_card = false;
	int i = 0;
	// xóa dữ liệu của lần gõ cũ
	for (i = 0; i < maximumKnocks; i++) {
		knockReadings[i] = 0;
	}

	int currentKnockNumber = 0;
	int startTime = millis();
	int now = millis();


	digitalWrite(greenLED, LOW); // Báo đèn theo tiếng gõ.
	if (programButtonPressed == true) {
		digitalWrite(redLED, LOW); // Và cả đèn đỏ nếu đang tạo kiểu gõ mới.
	}

	delay(knockFadeTime); // đợi một chút trước khi lắng nghe tiếng gõ tiếp theo.

	digitalWrite(greenLED, HIGH);
	if (programButtonPressed == true) {
		digitalWrite(redLED, HIGH);
	}

	do {
		//lắng nghe tiếng gõ tiếp theo cho đến khi hết thời gian
		knockSensorValue = analogRead(knockSensor);
		if (knockSensorValue >= threshold) { //thêm một tiếng gõ

			Serial.println("Gõ.");
			now = millis();
			knockReadings[currentKnockNumber] = now - startTime; //lưu lại khoảng thời gian so với lần gõ trước
			currentKnockNumber++;
			startTime = now; // reset lại thời gian bắt đầu

			digitalWrite(greenLED, LOW);
			if (programButtonPressed == true) {
				digitalWrite(redLED, LOW); // Hiện đèn đỏ nếu đang thiết lập bộ gõ mới
			}

			delay(knockFadeTime); // tiếp tục, đợi một chút trước khi lắng nghe tiếng gõ tiếp theo.

			digitalWrite(greenLED, HIGH);
			if (programButtonPressed == true) {
				digitalWrite(redLED, HIGH);
			}
		}

		now = millis();

		//nếu đã hết thời gian hoặc số lần gõ thì dừng vòng lặp
	} while ((now - startTime < knockComplete) && (currentKnockNumber < maximumKnocks)); //nếu có thẻ thì tự động thoát

	//đã thu thập được khiểu gõ hiện tai, tiến hành xác thực
	if (programButtonPressed == false) { // nếu không thiết lập kiểu gõ mới
		if (validateKnock() == true) { //nếu không sử dụng thẻ để mở thì tiếp tục
			triggerDoorUnlock("tiếng gõ");
		} else {
			Serial.println("Mở cửa thất bại.");
			digitalWrite(greenLED, LOW); // nếu mở cửa thất bại, nháy đèn đỏ báo hiệu.
			for (i = 0; i < 4; i++) {
				digitalWrite(redLED, HIGH);
				delay(100);
				digitalWrite(redLED, LOW);
				delay(100);
			}
			digitalWrite(greenLED, HIGH);
		}
	} else { //Nếu trong khi tạo thiết lập kiểu gõ mới, vẫn xác thực lại chỉ không mở cửa
		validateKnock();
		// và nháy đèn xanh/đỏ luân phiên để thể hiện đã được thiết lập
		Serial.println("New lock stored.");
		digitalWrite(redLED, LOW);
		digitalWrite(greenLED, HIGH);
		for (i = 0; i < 3; i++) {
			delay(100);
			digitalWrite(redLED, HIGH);
			digitalWrite(greenLED, LOW);
			delay(100);
			digitalWrite(redLED, LOW);
			digitalWrite(greenLED, HIGH);
		}
	}
}

//Chạy động cơ servo để mở khóa
void triggerDoorUnlock(const String& reason) {
	isLock = false;
	Serial.println(String("Đã mở cửa bằng ") + reason);
	myservo.write(90); // mở cửa
}

void triggerDoorlock() {
	Serial.println("Đã khóa cửa!");
	isLock = true;
	myservo.write(180); // khóa cửa
	shouldKeepDoorOpen = true;
}

void pressToOpen(const bool enablePressOutdoor = false) {
	int timeout   = 300;
	unsigned long now       = millis();

	bool pressedButton = programButtonPressed ? true : (programButtonPressed2 && enablePressOutdoor ? true : false);

	timePressStart = pressedButton && timePressStart == 0 ? now : timePressStart;

	if (pressedButton == false) {
		unsigned long time = now - timePressStart;

		if (time > 0 && time < timeout) {
			triggerDoorUnlock("nhấn nút");
		}

		timePressStart = 0;
	}
}

// Kiểm tra cách gõ gõ cửa
// trả về true nếu đúng và faslse nếu sai
bool validateKnock() {
	int i = 0;

	// thiết lập bộ đếm để so sánh số lần gõ cửa
	int currentKnockCount = 0;
	int secretKnockCount = 0;
	int maxKnockInterval = 0;

	for (i = 0; i < maximumKnocks; i++) {

		if (knockReadings[i] > 0) {
			currentKnockCount++; //đếm số lần gõ cửa
		}
		if (secretCode[i] > 0) {
			secretKnockCount++; //đếm số lần gõ bí mật
		}

		if (knockReadings[i] > maxKnockInterval) {
			maxKnockInterval = knockReadings[i];
		}
	}

	// Nếu thiết lập một kiểu gõ mới, lưu lại và thoát
	if (programButtonPressed == true) {
		for (i = 0; i < maximumKnocks; i++) { // normalize the times
			secretCode[i] = map(knockReadings[i], 0, maxKnockInterval, 0, 100);
		}
		// Nháy đèn theo khiểu gõ để báo hiệu kiểu gõ đã được thiết lập
		digitalWrite(greenLED, LOW);
		digitalWrite(redLED, LOW);
		delay(1000);
		digitalWrite(greenLED, HIGH);
		digitalWrite(redLED, HIGH);
		delay(50);
		for (i = 0; i < maximumKnocks; i++) {
			digitalWrite(greenLED, LOW);
			digitalWrite(redLED, LOW);
			// Chỉ bật đèn khi đó là một gõ
			if (secretCode[i] > 0) {
				delay(map(secretCode[i], 0, 100, 0, maxKnockInterval));
				digitalWrite(greenLED, HIGH);
				digitalWrite(redLED, HIGH);
			}
			delay(50);
		}
		return false; //không mở khóa cửa khi tạo một kiểu gõ mới
	}

	if (currentKnockCount != secretKnockCount) {
		return false;
	}

	// so sánh các khoảng thời gian tương đối của tiếng gõ cửa,

	int totaltimeDifferences = 0;
	int timeDiff = 0;
	for (i = 0; i < maximumKnocks; i++) { // Normalize the times
		knockReadings[i] = map(knockReadings[i], 0, maxKnockInterval, 0, 100);
		timeDiff = abs(knockReadings[i] - secretCode[i]);
		if (timeDiff > rejectValue) { // Nếu khoảng thời gian giữa lần gõ cửa chênh với khoảng thời gian tương đối
			return false;
		}
		totaltimeDifferences += timeDiff;
	}
	// Nó cũng có thể thất bại nếu gõ cửa không quá chính xác.
	if (totaltimeDifferences / secretKnockCount > averageRejectValue) {
		return false;
	}

	return true;

}

void STDIOProcedure(const String& command)
{
	hasRequest = command.startsWith("+IPD,");

	if(command.indexOf("GUARD_ON") != -1) {
		isEnableDoorGuard = true;

	} else if(command.indexOf("GUARD_OFF") != -1) {
		isEnableDoorGuard = false;

	} else if (command.indexOf("UNLOCK") != -1) {
		triggerDoorUnlock("web");
	}
}

void bufferingRequest(char c)
{
  static String bufferData = STDIO_STRING_EMPTY;

  switch (c)
  {
    case STDIO_CHAR_CR:
      break;
    case STDIO_CHAR_LF:
    {
      STDIOProcedure(bufferData);
      bufferData = STDIO_STRING_EMPTY;
    }
      break;
    default:
      bufferData += c;
  }
}

void deliverMessage(const String& msg, int dt)
{
  Serial.println(msg);
  delay(dt);
}

void initESP8266()
{
  deliverMessage("AT+RST", STDIO_DELAY_2X);
  deliverMessage("AT+CWMODE=2", STDIO_DELAY_3X);
  deliverMessage(String("AT+CWSAP=\"") + WIFI_NAME + String("\",\"") + WIFI_PASS + String("\",1,4"), STDIO_DELAY_3X);
  deliverMessage("AT+CIFSR", STDIO_DELAY_1X);
  deliverMessage("AT+CIPMUX=1", STDIO_DELAY_1X);
  deliverMessage(String("AT+CIPSERVER=1,") + STDIO_PROTOCOL_CURRENT, STDIO_DELAY_1X);
}
