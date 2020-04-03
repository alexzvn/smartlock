/* Detects patterns of knocks and triggers a motor to unlock
  it if the pattern is correct.
  By Steve Hoefer http://grathio.com
  Version 0.1.10.20.10
  Licensed under Creative Commons Attribution-Noncommercial-Share Alike 3.0
  http://creativecommons.org/licenses/by-nc-sa/3.0/us/
  (In short: Do what you want, just be sure to include this line and the four above it, and don't sell it or use it in anything you sell without contacting me.)
*/

#include <Servo.h>      // Thư viện điều khiển servo
#include <SPI.h>
#include "RFID.h"

#define SS_PIN 10
#define RST_PIN 9
//Định nghĩa chân pin
const int knockSensor = 0;   // Cảm biến áp điện
const int programSwitch = 2; // Nút nhấn
const int programSwitch2 = 7; // Nút nhấn
const int lockMotor = 8;     // Động cơ mở cửa
const int redLED = 4;        // LED trạng thái đỏ
const int greenLED = 5;      // LED trạng thái xanh
const int servoPin = 8;      // Chân điều khiển servo
const int hallPin  = 6;      // Chân cảm biến từ trường

//Cẫu hình nhận biết
const int threshold = 25;          // tín hiệu nhỏ nhất để xác định là một tiếng gõ
const int rejectValue = 25;        // Tỉ lệ phần trăm khác nhau giữa khoảng thời gian một tiếng gõ, nếu lớn hơn thì không mở khóa
const int averageRejectValue = 15; // If the average timing of the knocks is off by this percent we don't unlock.
const int knockFadeTime = 150;     // milis dây delay trước khi lắng nghe tiếng gõ tiếp theo

const int maximumKnocks = 20;      // Số lần gõ cửa tối đa cho một kiểu gõ
const int knockComplete = 1200;    // Thời gian chờ lâu nhất để xác nhận gõ cửa xong

//Cẫu hình RIFD (đọc thẻ)
RFID rfid(SS_PIN, RST_PIN);
unsigned char i, j;
unsigned char reading_card[5];
unsigned char master[5] = {217, 146, 218, 62, 175}; // Mã Card phù hợp để mở cửa
unsigned char slave[5] = {57, 51, 164, 174, 0};     // Mã Card phù hợp để đóng cửa

// Biến.
int secretCode[maximumKnocks] = {1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int knockReadings[maximumKnocks]; // Khi ai đó gõ cửa, biến này sẽ lưu lại khoảng cách giữa mỗi lần gõ
int knockSensorValue = 0; // Lưu lại giá trị trả về từ cảm biến áp điện.
int programButtonPressed = false;
int hallValue = 0;  //giá trị cảm biến từ trường
int isLock = true;  //đã khóa cửa hay chưa
int timePressStart = 0;
int pressTime   = 0;

//Cẫu hình động cơ servo
const int servoUnlock = 90;
const int servoLock = 120;
Servo myservo;

//***************************************************
//*           WIFI CONFIG                           *
//***************************************************

bool hasRequest = false;

#define CMD_SEND_BEGIN    "AT+CIPSEND=0"
#define CMD_SEND_END    "AT+CIPCLOSE=0"

#define PROTOCOL_HTTP     80
#define PROTOCOL_HTTPS    443
#define PROTOCOL_FTP      21
#define PROTOCOL_CURRENT  PROTOCOL_HTTP

#define CHAR_CR     0x0D
#define CHAR_LF     0x0A

#define STRING_EMPTY  ""

#define DELAY_SEED  1000
#define DELAY_1X    (1*DELAY_SEED)
#define DELAY_2X    (2*DELAY_SEED)
#define DELAY_3X    (3*DELAY_SEED)
#define DELAY_4X    (4*DELAY_SEED)
#define DELAY_5X    (5*DELAY_SEED)

#define WIFI_NAME "doom"
#define WIFI_PASS "12345678"

void setup() {
	delay(DELAY_5X);

	pinMode(lockMotor, OUTPUT);
	pinMode(redLED, OUTPUT);
	pinMode(greenLED, OUTPUT);
	pinMode(programSwitch, INPUT);
	pinMode(hallPin, INPUT);

	Serial.begin(115200);

	myservo.attach(servoPin);
	SPI.begin();
	rfid.init();

	digitalWrite(greenLED, HIGH); // Để đèn xanh trong trạng thái chờ
	triggerDoorlock();
  delay(3000);
  initESP8266();
}

void loop() {

  // lắng nghe cảm biến, nếu có tiếng gõ
  knockSensorValue = analogRead(knockSensor);

  if (digitalRead(programSwitch) == HIGH) { // nếu đã nhấn nút
    programButtonPressed = true; // lưu lại sự kiện
    digitalWrite(redLED, HIGH); // và hiện đèn đỏ
  } else {
    programButtonPressed = false;
    digitalWrite(redLED, LOW);
  }

  while(Serial.available())
  {
    bufferingRequest(Serial.read());
  }

  if(hasRequest == true)
  {
    String htmlResponse ="<!doctype html>"
          "<html>"
          "<head>"
            "<title>Door DEMO</title>"
          "</head>"
          "<body>"
            "<h1>Door Remote DEMO</h1>"
            "<h3><a href='http://192.168.4.1/?DOOR=DOOR_ON_ON_ON'>Mo Khoa </a></h3>"
          "</body>"
          "</html>";

    String beginSendCmd = String(CMD_SEND_BEGIN) + "," + htmlResponse.length();
    sendCommand(beginSendCmd, DELAY_1X);
    sendCommand(htmlResponse, DELAY_2X);
    sendCommand(CMD_SEND_END, DELAY_1X);
    hasRequest = false;
  }

	if (isLock == true) { //Nếu cửa đã khóa
		pressToOpen();
		if (knockSensorValue >= threshold) { // nếu có gõ cửa
			//Serial.println(knockSensorValue);
			listenToSecretKnock();
		} else {
			//isCard(); //check xem có thẻ hay không
		}
	}

	if(digitalRead(hallPin) == LOW && isLock == false)  //Nếu cửa chưa đóng và chưa khóa
	{
		triggerDoorlock();
	}
}

// Sự kiện khi người dùng gõ cửa
void listenToSecretKnock() {

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
	} while ((now - startTime < knockComplete) && (currentKnockNumber < maximumKnocks) && isCard() == false); //nếu có thẻ thì tự động thoát

	//đã thu thập được khiểu gõ hiện tai, tiến hành xác thực
	if (programButtonPressed == false) { // nếu không thiết lập kiểu gõ mới
		if (validateKnock() == true && isCard() == false) { //nếu không sử dụng thẻ để mở thì tiếp tục
			triggerDoorUnlock();
		} else {
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
void triggerDoorUnlock() {
	isLock = false;
	myservo.write(90); // mở cửa
	delay(3000);
}

void triggerDoorlock() {
	isLock = true;
	myservo.write(180); // khóa cửa
	delay(2000);
}

void pressToOpen() {
	int timeout   = 300;
	int now       = millis();

	timePressStart = programButtonPressed && timePressStart == 0 ? now : timePressStart;

	if (programButtonPressed == false) {
		int time = now - timePressStart;

		if (time > 0 && time < timeout) {
			triggerDoorUnlock();
		}

		timePressStart = 0;
	}
}

// Kiểm tra cách gõ gõ cửa
// trả về true nếu đúng và faslse nếu sai
boolean validateKnock() {
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

boolean isCard() {
	if (rfid.isCard()) {
		if (rfid.readCardSerial()) // Nếu có thẻ
		{
			for (i = 0; i < 5; i++) {
				reading_card[i] = rfid.serNum[i];
			}

			//xác thực
			for (i = 0; i < 5; i++) {
				//So sáng từng phần tử của mảng reading_card với mảng master
				if (reading_card[i] != master[i]) //Nếu có 1 phần tử bất kỳ nào không phù hợp...thỳ thoát vòng lặp, lúc này ta nhận được giá trị của i
				{
					break;
				}
			}

			// Tương tự với thẻ Slave
			for (j = 0; j < 5; j++) {
				if (reading_card[i] != slave[i]) {
					break;
				}
			}

			if (j == 5) // Nếu các phần tử của màng reading_card phù hợp hết với mảng master thì lúc này i chạy đến 5
			{//thẻ slave
				//triggerDoorlock();
				return true;
			}

			if (i == 5) { //thẻ master
				triggerDoorUnlock();
				return true;
			}

			rfid.halt();
		}
	}

	return false;
}


void initESP8266()
{
  sendCommand("AT+RST", DELAY_2X);
  sendCommand("AT+CWMODE=2", DELAY_3X);
  sendCommand(String("AT+CWSAP=\"") + WIFI_NAME + String("\",\"") + WIFI_PASS + String("\",1,4"), DELAY_3X);
  sendCommand("AT+CIFSR", DELAY_1X);
  sendCommand("AT+CIPMUX=1", DELAY_1X);
  sendCommand(String("AT+CIPSERVER=1,") + PROTOCOL_CURRENT, DELAY_1X);
}

void bufferingRequest(char c)
{
  static String bufferData = STRING_EMPTY;

  switch (c)
  {
    case CHAR_CR:
      break;
    case CHAR_LF:
    {
      STDIOProcedure(bufferData);
      bufferData = STRING_EMPTY;
    }
      break;
    default:
      bufferData += c;
  }
}

void STDIOProcedure(const String& command)
{
  hasRequest = command.startsWith("+IPD,");

  if(command.indexOf("DOOR_ON_ON_ON") != -1)
  {
    triggerDoorUnlock();
  }
}

void sendCommand(const String& msg, int dt)
{
  Serial.println(msg);
  delay(dt);
  while(Serial.available())
  {
    Serial.read();
  }
}