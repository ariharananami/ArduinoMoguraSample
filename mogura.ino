#include <Servo.h>
#include <Wire.h>
#include <MMA8653.h>
#include <Clock.h>
#include <Studuino.h>

Studuino board;

// リアルタイム処理で時刻を管理するクラス
class LyricalTimer {
public:
	using time_t = unsigned long;

	inline bool IsFired() {
		return millis() > nextProcTime && ++firedCount;
	}
	inline void SetNext(time_t during) {
		nextProcTime = millis() + during;
	}
	inline void SetDisable() {
		nextProcTime = (time_t) -1; // 簡易的に符号なし整数の最大値を代入
	}
	inline int GetFiredCount() {
		return firedCount;
	}
	inline void ResetFiredCount() {
		firedCount = 0;
	}
private:
	time_t nextProcTime = 0;
	int firedCount = 0;				// IsFiredでの確認時、発火していた累計回数
};


// ボタンが押された長さを数えることで押し離しを検出するクラス
class LyricalButton {
	int pressedDuration = 0;
public:
	int Update(bool isPressed) {
		if (isPressed) {
			// 押されているときは、インクリメント
			pressedDuration++;
		} else {
			// 推されてないときは、直前まで押されていれば-1でそれ以外は0
			pressedDuration = (pressedDuration > 0) ? -1 : 0;
		}
		return 0;
	}
	inline int GetPressedDuration() {
		return pressedDuration;
	}
	inline bool IsPressed(){
		return pressedDuration > 0;	// IsClickedと同時にフラグがたつので注意
	}
	inline bool IsClicked(){
		return pressedDuration == 1;
	}
	inline bool IsReleased(){
		return pressedDuration == -1;
	}
};


// もぐら叩きのモグラ。LEDの初期化などは個別に行う点に注意
class MoguraDevice {
	const int PIN_LED;		// LEDのピン
	const int PIN_BUTTON;	// ボタンのピン

	bool isOutside = false;
	LyricalTimer ledTimer;
	LyricalButton button;

public:
	MoguraDevice(int pinLed, int pinButton):
		PIN_LED(pinLed),
		PIN_BUTTON(pinButton)
	{
	}

	// いきなり始めないようにする
	void Start() {
		ledTimer.SetNext(random(100, 3000));	// 最初は100～2000ms後にセット
	}

	// 毎フレーム呼び出す更新処理
	template<class Fn1, class Fn2>			// ラムダ式をfunctionalを使わず引数にとるためのおまじない
	void Update(Fn1 okHandler, Fn2 ngHandler) {
		// あたり判定
		button.Update(digitalRead(PIN_LED) == 0);
		if (button.IsClicked()) {
			// あたり：押されたときにもぐらが出ていた
			if (isOutside) {
				Down();
				okHandler();				// あたったときのコールバック
			}
			// はずれ：押されたけどもぐらがひっこんでいた
			else {
				ngHandler();				// はずれたときのコールバック
			}
		}

		// 出現処理
		if (ledTimer.IsFired()) {
			if (isOutside) {
				Down();
				int during = (random(1200, 5000) + random(1200, 5000)) / 2;	// 平均をとってコクのある乱数に
				ledTimer.SetNext(during);
			}
			else {
				Up();
				ledTimer.SetNext(random(100, 1000));
			}
		}
	}

	// 初期状態に戻す
	void Reset() {
		Down();
	}

private:
	void Up() {
		isOutside = true;
		digitalWrite(PIN_BUTTON, HIGH);
	}
	void Down() {
		isOutside = false;
		digitalWrite(PIN_BUTTON, LOW);
	}
};


enum class Sequence {
	Initialize,
	Countdown,
	Playing,
	Finish,
};
Sequence currentSequence = Sequence::Initialize;

MoguraDevice moguras[] = {
	MoguraDevice(14, 2),
	MoguraDevice(15, 4),
	MoguraDevice(16, 7)
};

void setup() {
	randomSeed(analogRead(0));	// 乱数シード生成
	pinMode(13, OUTPUT);		// オンボードLED
	Serial.begin(115200);

	// モグラ
	pinMode(2, OUTPUT); 		// D2
	pinMode(4, OUTPUT); 		// D4
	pinMode(7, OUTPUT); 		// D7
	pinMode(14, INPUT); 		// A0
	pinMode(15, INPUT); 		// A1
	pinMode(16, INPUT); 		// A2

	board.InitSensorPort(PORT_A4, PIDBUZZER);

	currentSequence = Sequence::Countdown;
}

LyricalTimer countdownTimer;
LyricalTimer playingTimer;
int score = 0;

void loop() {
	switch (currentSequence)
	{
	case Sequence::Initialize:
		// 到達しないはずのコード
		break;

	case Sequence::Countdown:
		if (countdownTimer.IsFired()) {
			const int phase = countdownTimer.GetFiredCount();	// IsFireがtrueになった回数 (≧ 1)
			constexpr int LED_PIN[] = {2, 4, 7};
			// 1,2,3個のLEDを順番に点灯して、
			if (1 <= phase && phase <= 3) {
				const int n = phase;	// index:0～n-1のLEDを点灯
				for (int i = 0; i < n; ++i)
					digitalWrite(LED_PIN[i], HIGH);
				for (int i = n; i < 3; ++i)
					digitalWrite(LED_PIN[i], LOW);
				board.Buzzer(PORT_A4, BZR_C5, 200);
				countdownTimer.SetNext(1000);
			}
			// 一瞬だけ全消灯してから、
			else if (phase == 4) {
				for (int i = 0; i < 3; ++i)
					digitalWrite(LED_PIN[i], LOW);
				countdownTimer.SetNext(150);
			}
			// 最後に少し長く全点灯
			else if (phase == 5) {
				for (int i = 0; i < 3; ++i)
					digitalWrite(LED_PIN[i], HIGH);
				board.Buzzer(PORT_A4, BZR_C6, 800);
				countdownTimer.SetNext(800);
			}
			// goto next sequence
			else {
				for (int i = 0; i < 3; ++i)
					digitalWrite(LED_PIN[i], LOW);
				for(auto&& mogura : moguras)
					mogura.Start();
				currentSequence = Sequence::Playing;
			}
		}
		break;

	case Sequence::Playing:
		if (playingTimer.IsFired()) {
			auto okHandler = [&]() {
				score += 1;
				board.Buzzer(PORT_A4, BZR_C7, 200);
				Serial.println(score);
			};
			auto ngHandler = [&]() {
				score -= 1;
				board.Buzzer(PORT_A4, BZR_C4, 600);
				Serial.println(score);
			};
			// 各モグラの処理
			for(auto&& mogura : moguras) {
				mogura.Update(okHandler, ngHandler);
			}

			// スコアが10以上になったらゲームクリア
			if (score >= 10) {
				for(auto&& mogura : moguras)
					mogura.Reset();
				currentSequence = Sequence::Finish;
			}
			playingTimer.SetNext(20);
		}
		break;

	case Sequence::Finish:
		// 終わっても特に何もしない
		break;
	}

}
