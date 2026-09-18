# 01. 진동/센서 이상 탐지 (Anomaly Detection)

## 이 실습의 목표

정상 상태의 센서 값만 가지고 학습한 **Autoencoder** 모델을 ESP32-S3에 올려, 평소와 다른 패턴이 나타나면 실시간으로 "이상(anomaly)"이라고 판정하는 온디바이스 AI 파이프라인을 처음부터 끝까지 직접 만들어봅니다. 데이터 수집(보드) → 모델 학습(PC) → 양자화/변환(PC) → 추론(보드)까지 TinyML 프로젝트의 전형적인 4단계를 모두 경험하는 것이 목표입니다.

이 실습은 [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md)에서 만든 ESP-IDF 개발환경이 이미 되어 있다는 전제로 진행합니다. 아직 안 하셨다면 00번을 먼저 완료해주세요.

## 준비물

- ESP32-S3-DevKitC-1 (N16R8)
- **HW-664 모듈 (LIS3DH 3축 가속도계, I2C)** 1개
- **SSD1306 OLED 모듈 (128x64, I2C)** 1개 — 선택사항. 지금 어떤 단계(데이터 수집/추론)가 돌고 있는지, 실시간 상태(정상/이상)를 화면에 보여주는 용도입니다. 없어도 시리얼 모니터로 모든 정보를 확인할 수 있으니 실습 자체에는 필수가 아닙니다.
- PC에 Python 3.9 이상 + TensorFlow 설치 (모델 학습은 보드가 아니라 **PC에서** 진행합니다)

### 배선 (I2C0 + I2C1, 2개 버스 동시 사용)

LIS3DH와 SSD1306을 **서로 다른 I2C 버스**에 나눠 답니다 — ESP32-S3는 I2C 컨트롤러가 2개(I2C0, I2C1)라서 각각 독립적으로 쓸 수 있습니다. 아래 핀은 스트래핑 핀이나 USB-JTAG 핀과 겹치지 않는, 안전하게 쓸 수 있는 GPIO로 골랐습니다.

| 모듈 | 핀 | ESP32-S3 GPIO | I2C 버스 |
|---|---|---|---|
| HW-664 (LIS3DH) | SDA | GPIO8 | I2C0 |
| HW-664 (LIS3DH) | SCL | GPIO9 | I2C0 |
| SSD1306 (OLED) | SDA | GPIO4 | I2C1 |
| SSD1306 (OLED) | SCL | GPIO5 | I2C1 |
| 공통 | VCC | 3.3V | — |
| 공통 | GND | GND | — |

> HW-664 모듈은 **LIS3DH** 칩입니다 (일부 유통 문서에는 LIS3DSH로 잘못 표기되기도 하는데, `WHO_AM_I` 레지스터 값이 `0x33`으로 확인되면 LIS3DH가 맞습니다 — LIS3DSH는 다른 값을 반환합니다). I2C 주소는 이 모듈에서 **0x19**로 확인되었습니다(SDO/SA0 핀이 풀업된 상태) — 만약 다른 개체를 쓰시는데 응답이 없다면 SDO 핀 상태에 따라 0x18일 수도 있으니 아래 트러블슈팅을 참고하세요.
>
> SSD1306의 I2C 주소는 모듈마다 0x3C 또는 0x3D인 경우가 있어, 코드에서 부팅 시 두 주소를 순서대로 프로브해서 자동으로 찾습니다 — 별도 설정 없이 그냥 연결만 하면 됩니다.

## 상태 표시용 OLED (선택사항)

`lab/01_data_collect`와 `lab/03_inference` 양쪽 모두에 `src/oled_status.c`/`oled_status.h`라는 아주 작은 SSD1306 드라이버가 포함되어 있습니다 (128x64, I2C1, 8x8 텍스트). ESP-IDF에는 표준 디스플레이 드라이버 스택이 기본 제공되지 않아서, 이 실습에 필요한 최소 기능(화면 지우기 + 최대 3줄 텍스트 출력)만 직접 구현했습니다.

- `oled_status_init()`: 부팅 시 한 번 호출 — SSD1306이 없거나 응답하지 않아도 **에러 없이 조용히 비활성화**되고 나머지 프로그램은 정상 동작합니다 (필수 하드웨어가 아니라는 뜻).
- `oled_status_show(title, line1, line2)`: 화면을 지우고 3줄을 표시합니다. 한 줄은 최대 16글자(초과분은 잘림).
- **Step 1(`01_data_collect`)**: 1번째 줄에 `LAB01 Step1`, 2번째 줄에 `Collecting...`, 3번째 줄에 지금까지 수집한 윈도우 개수(`windows: N`)를 실시간으로 갱신합니다.
- **Step 4(`03_inference`)**: 1번째 줄에 `LAB01 Step4`, 2번째 줄에 `normal` 또는 `** ANOMALY **`, 3번째 줄에 방금 계산된 복원오차(`mse=0.0123`)를 매 추론마다 갱신합니다 — 지금 이 순간 정상인지 이상인지 화면만 보고 바로 알 수 있습니다.

다른 실습(02번)에 이 화면을 재사용하고 싶으시면 `oled_status.h`/`oled_status.c`/`font8x8_basic.h` 세 파일을 그대로 복사해서 쓰시면 됩니다 (I2C1/GPIO4·5 배선이 이미 되어 있다는 전제).

## 개념 — Autoencoder 기반 이상 탐지

**Autoencoder**는 입력을 압축했다가(encoder) 그대로 복원(decoder)하도록 학습시키는 신경망입니다. "정상" 데이터만으로 학습시키면, 정상 패턴에 대해서는 복원이 잘 되지만(복원 오차가 작음), 학습 때 한 번도 보지 못한 비정상 패턴이 들어오면 복원이 잘 안 됩니다(복원 오차가 큼). 이 복원 오차가 어떤 임계값(threshold)을 넘으면 "이상"으로 판정합니다.

이 방식의 핵심 장점은 **비정상 데이터를 따로 수집할 필요가 없다는 것**입니다 — 정상 데이터만 있으면 학습이 가능합니다. 실제 현장에서는 "고장 사례"를 미리 수집하는 것 자체가 어려운 경우가 많기 때문에(고장이 자주 안 남, 고장 원인이 다양함), 이 접근이 실용적으로 많이 쓰입니다.

전체 흐름은 다음과 같습니다.

```
[보드] LIS3DH에서 X/Y/Z 가속도 읽기 → 시리얼로 CSV 출력
   ↓
[PC]  CSV 로드 → Autoencoder 학습 → 정상 데이터의 복원오차 분포로 threshold 결정
   ↓
[PC]  모델을 INT8로 양자화 → .tflite → C 배열(.h)로 변환
   ↓
[보드] .h 파일을 프로젝트에 포함 → TFLM으로 실시간 추론 → 복원오차 계산 → threshold와 비교
```

## Step 1. 데이터 수집 (ESP-IDF, I2C로 LIS3DH 읽기)

`lab/01_data_collect/`에 준비된 PlatformIO ESP-IDF 프로젝트를 엽니다. 소스는 `src/main.c`에 있습니다 (PlatformIO는 순정 ESP-IDF의 `main/` 대신 `src/`를 기본 소스 폴더로 씁니다 — 00번 문서 참고).

`src/main.c`의 핵심 로직:

```c
#define SAMPLE_WINDOW 20   // 20개의 (x,y,z) 샘플을 하나의 "패턴"으로 묶음

// CTRL_REG1: ODR=100Hz, 정상 전력 모드, X/Y/Z 축 모두 활성화
lis3dh_write_reg(LIS3DH_REG_CTRL_REG1, 0x57);

while (1) {
    for (int i = 0; i < SAMPLE_WINDOW; i++) {
        int16_t x, y, z;
        lis3dh_read_xyz(&x, &y, &z);           // OUT_X_L부터 6바이트 자동증가 읽기
        printf("%d,%d,%d", x, y, z);
        if (i < SAMPLE_WINDOW - 1) printf(",");
        vTaskDelay(pdMS_TO_TICKS(20));         // 20ms 간격
    }
    printf("\n");
}
```

새 I2C 마스터 드라이버(`driver/i2c_master.h`, ESP-IDF v5.x 표준 API)로 버스와 디바이스를 초기화하고, 부팅 직후 `WHO_AM_I`(0x0F 레지스터) 값이 `0x33`인지 확인해서 배선/주소가 맞는지 검증하는 코드가 이미 포함되어 있습니다. SSD1306이 연결되어 있다면(I2C1) 부팅 직후 화면에 "Collecting..."과 실시간 수집 윈도우 개수가 표시됩니다 — 자세한 내용은 위 "상태 표시용 OLED" 절 참고.

빌드/업로드 후, PC에서 시리얼 로그를 텍스트 파일로 저장합니다.

```bash
cd 01_data_collect
pio run -t upload
pio device monitor > normal_data.csv
```

**정상 상태**로 최소 수백~수천 줄을 모으세요 — 보드를 평소 다루는 방식대로 천천히/규칙적으로 움직이거나, 실제 배포 환경에서 겪을 정상적인 진동(예: 선풍기 위, 모터 옆) 위에 올려두고 기록하면 됩니다. 이 시점에서는 "비정상" 데이터를 따로 만들 필요가 없습니다 (Autoencoder 개념 절 참고). 원하는 만큼 모았으면 `Ctrl+C`로 모니터를 종료하고, 생성된 `normal_data.csv`를 `lab/02_train_autoencoder/` 폴더로 옮깁니다.

> **중요**: "정상"을 책상 위에 완전히 가만히 세워둔 상태로만 수집하면 안 됩니다. Autoencoder는 학습 데이터에 나온 패턴만 "정상"으로 배우기 때문에, 데이터가 너무 조용/정적이면 복원 오차의 편차(표준편차)가 거의 0에 가까워지고, 그 결과 Step 2에서 계산되는 threshold(평균+3×표준편차)도 지나치게 작아집니다. 이러면 실제 사용 중에 생기는 정상적인 손떨림/흔들림 수준의 편차조차 threshold를 넘어버려서 **계속 ANOMALY만 뜨는 상태**가 됩니다. 실제로 이 실습을 진행하면서 흔히 겪는 문제이니, 정상 데이터를 모으실 때 실제 배포 환경에서 있을 법한 정도의 움직임/진동 편차를 의도적으로 포함시켜 수집하세요 (아래 트러블슈팅의 "항상 ANOMALY만 뜸" 항목도 참고).

> **참고**: 첫 줄과 마지막 줄은 값 개수가 60개(SAMPLE_WINDOW×AXES)가 아닐 수 있습니다. `pio device monitor`를 시작한 시점이 보드가 이미 한 윈도우를 출력하는 도중일 수 있어 첫 줄이 잘려서 시작되고, `Ctrl+C`로 종료하는 시점도 마찬가지로 마지막 윈도우 출력 중간일 수 있기 때문입니다. Step 2의 `train_autoencoder.py`는 이런 줄을 자동으로 걸러내고 몇 줄을 건너뛰었는지 알려주므로, **직접 지우지 않고 그대로 두셔도 됩니다.**

## Step 2. Python으로 Autoencoder 학습 (PC에서, 보드와 무관)

`lab/02_train_autoencoder/train_autoencoder.py`를 실행합니다. 먼저 의존성을 설치합니다.

> PC에 이미 다른 Python 프로젝트들이 설치되어 있다면, 이 랩만을 위한 가상환경(venv)을 만들어서 설치하는 것을 권장합니다. 그래야 다른 프로젝트가 이미 깔아둔 패키지와 버전이 꼬여서 나는 `pip`의 의존성 충돌 경고를 피할 수 있습니다 (아래 트러블슈팅 참고).
>
> ```bash
> cd 02_train_autoencoder
> python -m venv venv
> # Windows
> venv\Scripts\activate
> # macOS/Linux
> source venv/bin/activate
> ```

```bash
pip install -r requirements.txt
python train_autoencoder.py
```

스크립트는 다음을 수행합니다.

1. `normal_data.csv`를 한 줄씩 읽으면서, 값 개수가 60개(SAMPLE_WINDOW×AXES)가 아닌 줄(주로 첫 줄/마지막 줄)은 자동으로 건너뛰고 몇 줄을 건너뛰었는지 출력 — 그 다음 -1~1 범위로 정규화 (LIS3DH raw 16bit 값 ÷ 32768.0)
2. 60(=20샘플×3축) → 16 → 60 구조의 작은 Autoencoder를 50 epoch 학습
3. 정상 데이터의 복원 오차 분포를 출력하고, `평균 + 3×표준편차`를 추천 threshold로 계산
4. 모델을 INT8로 양자화하여 `anomaly_model.tflite`로 저장

실행 결과에 출력되는 **추천 threshold 값을 반드시 메모해두세요** — Step 4에서 코드에 그대로 넣습니다.

## Step 3. 모델을 C 배열로 변환

```bash
xxd -i anomaly_model.tflite > anomaly_model.h
```

`xxd`는 리눅스/macOS/WSL에 기본 포함되어 있습니다. Windows에서 WSL 없이 진행 중이라면, Git Bash에 포함된 `xxd`를 쓰거나 Python으로 동일한 변환을 할 수 있습니다.

```python
# xxd -i 가 없을 때의 대안 (Python)
with open("anomaly_model.tflite", "rb") as f:
    data = f.read()
with open("anomaly_model.h", "w") as f:
    f.write("unsigned char anomaly_model_tflite[] = {\n")
    f.write(",".join(f"0x{b:02x}" for b in data))
    f.write("\n};\n")
    f.write(f"unsigned int anomaly_model_tflite_len = {len(data)};\n")
```

생성된 `anomaly_model.h`를 `lab/03_inference/src/` 폴더에 넣습니다 (기존 placeholder 파일을 덮어씁니다).

## Step 4. ESP32-S3에서 추론

`lab/03_inference/`의 `src/main.cc`에서 `ANOMALY_THRESHOLD` 값을 Step 2에서 구한 threshold로 교체합니다.

```c
#define ANOMALY_THRESHOLD 0.02f   // Step 2에서 구한 threshold로 교체
```

I2C 초기화/`lis3dh_read_xyz()` 코드는 Step 1과 동일한 로직을 그대로 씁니다. SSD1306이 연결되어 있다면 매 추론마다 `normal` 또는 `** ANOMALY **`와 복원오차 값이 화면에 실시간으로 갱신됩니다.

```bash
cd 03_inference
pio run -t upload
pio device monitor
```

## 실행 & 확인

- 평소처럼 다루면 `normal, mse=...`이 계속 출력되는지 확인합니다.
- 평소와 다른 패턴(갑자기 세게 흔들기, 툭툭 치기, 평소 안 두던 자세로 세워두기 등)을 만들어보면 `ANOMALY DETECTED! mse=...`가 뜨는지 확인합니다.

## 관찰 포인트

- 이 파이프라인의 입력은 "20개 샘플 × 3축 = 60차원 벡터"입니다. 축을 더 늘리거나(예: 자이로 추가), 샘플 개수를 늘리면 그만큼 입력 차원과 encoder/decoder 크기만 조정하면 되고, 나머지 학습/양자화/배포 흐름은 동일합니다.
- 임계값(threshold)은 한 번 정하고 끝이 아닙니다 — 실제 배포 환경에서 정상 오차가 얼마나 변동하는지 보고 조정해야 합니다. 너무 낮으면 오탐(false positive)이 잦고, 너무 높으면 진짜 이상을 놓칩니다. 특히 학습 데이터를 너무 조용한 상태에서만 모았다면 threshold 자체가 비정상적으로 작게 나와서 계속 오탐이 나는데, 이건 코드 버그가 아니라 **데이터 수집 단계에서 "정상"의 범위를 너무 좁게 정의한 것**이 원인입니다 — 데이터를 다시 모으거나(권장), 급하면 threshold 배수(`errors.mean() + 3 * errors.std()`의 `3`)를 4~5 정도로 늘려서 임시로 완화할 수 있습니다.
- `MicroMutableOpResolver<3>`처럼 필요한 연산(Op)만 등록하는 이유는, TFLM이 모든 연산을 다 포함하면 플래시 용량을 많이 차지하기 때문입니다 — 이 모델은 FullyConnected + Relu(encoder) + Tanh(decoder 출력)만 쓰므로 이 세 가지만 등록합니다. Decoder의 마지막 활성화 함수를 sigmoid가 아니라 **tanh**로 바꾼 이유는, 가속도 raw 값이 ADC와 달리 음수/양수 모두 나올 수 있어 정규화 범위가 -1~1이기 때문입니다(sigmoid는 0~1만 출력).
- LIS3DH는 `CTRL_REG1`에서 출력 데이터 레이트(ODR)를 최대 5.3kHz까지 낼 수 있는 센서입니다 — 이 실습은 100Hz로 충분하지만, 더 빠른 진동(모터 베어링 결함 등)을 감지하려면 ODR을 높이고 `SAMPLE_PERIOD_MS`도 그에 맞춰 줄이면 됩니다.

## 다른 방식으로 확장하기

**SW-420/노크 센서(디지털 진동 스위치)로 바꾸는 경우**: 이 센서는 가속도 연속값이 아니라 "이벤트 발생 여부"를 주는 디지털 센서입니다. `lis3dh_read_xyz()` 대신, `SAMPLE_WINDOW` 슬롯 동안 인터럽트가 몇 번 발생했는지 **카운트**해서 하나의 값으로 만드는 방식이 자연스럽습니다.

```c
// 매 슬롯마다 축 3개를 읽는 대신, 예: 400ms 윈도우 동안의 이벤트 카운트를 특징값으로 사용
volatile int event_count = 0;   // ISR에서 증가
// ... 윈도우 종료 시 event_count를 window[i] 자리에 넣고 0으로 리셋
```

정상 상태(평소 진동 수준)에서는 카운트가 낮고 일정하게 유지되지만, 이상 상태(충격, 고장 등)에서는 카운트가 급증하는 패턴을 Autoencoder가 학습하게 됩니다. 이 경우 입력 차원은 3축이 아니라 1차원(카운트)이 되므로, `train_autoencoder.py`의 `AXES`를 1로 바꾸면 됩니다.

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `WHO_AM_I` 값이 `0x33`이 아님 | 배선(SDA/SCL이 반대로 연결됐는지) 또는 I2C 주소 확인 — SDO/SA0 핀이 GND에 연결되어 있으면 주소가 `0x18`일 수 있습니다. `LIS3DH_ADDR`을 `0x18`로 바꿔서 재시도 |
| `i2c_master_transmit` 관련 에러 (`ESP_ERR_TIMEOUT` 등) | I2C 풀업 저항 확인 (모듈에 내장되어 있지 않다면 SDA/SCL에 4.7kΩ 정도의 외부 풀업 필요), 배선 접촉 확인 |
| `AllocateTensors() failed` | `kTensorArenaSize`가 모델이 필요로 하는 크기보다 작음 — 값을 늘려보며 재시도 |
| 항상 ANOMALY만 뜸 | **가장 흔한 원인은 학습 데이터 수집 문제입니다**: "정상" 데이터를 보드를 완전히 가만히 세워둔, 너무 조용/정적인 상태로만 모으면 복원 오차의 표준편차가 거의 0에 가까워지고, Step 2가 계산하는 threshold(평균+3×표준편차)도 지나치게 작아져서 실제 사용 중의 정상적인 편차조차 다 ANOMALY로 걸립니다 — 해결은 실제 배포 환경에서 있을 법한 정도의 움직임/진동을 포함해서 정상 데이터를 다시 수집하는 것(위 Step 1의 참고 박스 참고). 데이터 자체는 괜찮은데도 계속 그렇다면, 정규화(÷32768.0) 기준이 학습 때와 다르거나 양자화 scale/zero_point 적용이 잘못됐을 가능성도 확인하세요 — Step 2의 정규화 방식과 Step 4가 정확히 일치하는지 확인 |
| 학습은 잘 됐는데 임베디드에서 결과가 이상함 | Python(float32)과 ESP32(int8 양자화) 사이의 정밀도 차이 — 임계값을 임베디드에서 실측한 정상 오차 기준으로 재조정 |
| `Failed to resolve component 'tflite-lib'` (03_inference 빌드 시) | `src/idf_component.yml` 위치/문법 확인 — [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md) 트러블슈팅 참고 |
| `03_inference` 빌드 시 `error: either all initializer clauses should be designated or none of them should be` / `expected primary-expression before '.' token` (`main.cc`) | C++ 문법 이슈입니다 — `.flags.enable_internal_pullup = true`처럼 중첩된 멤버를 점(`.`) 두 개로 한 번에 지정하는 표기는 **C에서는 GCC 확장으로 허용**되지만 **C++ 표준에서는 허용되지 않습니다**. `01_data_collect`는 `main.c`(C)라 문제없이 컴파일되지만, `03_inference`는 `main.cc`(C++)라 이 표기에서 에러가 납니다. `.flags.enable_internal_pullup = true,` 를 `.flags = { .enable_internal_pullup = true },`처럼 중첩 중괄호로 감싸면 C/C++ 모두에서 정상 컴파일됩니다(이미 코드에 반영되어 있습니다) |
| `03_inference` 빌드 시 `error: designator order for field '...' does not match declaration order in '...'` (`main.cc`) | 역시 C++(C++20) 고유 규칙입니다 — **C는 designated initializer(`.field = value`)를 원하는 순서로 써도 되지만, C++20은 구조체가 선언된 순서 그대로 써야 합니다.** `i2c_master_bus_config_t`의 실제 선언 순서는 `i2c_port → sda_io_num → scl_io_num → clk_source → glitch_ignore_cnt → ... → flags`인데, 처음 코드는 `clk_source`를 `i2c_port`보다 앞에 쓰는 등 순서가 달라 에러가 났습니다. `main.cc`의 초기화 순서를 실제 선언 순서에 맞게 고쳤습니다(이미 코드에 반영되어 있습니다). **일반화하면**: ESP-IDF 구조체를 C++(.cc/.cpp) 파일에서 이렇게 필드별로 초기화할 때는, 생략 가능한 필드를 빼더라도 **쓰는 필드들의 상대적 순서가 헤더 파일의 선언 순서와 같아야** 합니다 — C 파일(`main.c`, `oled_status.c`)은 이 제약이 없어서 그대로 두어도 됩니다 |
| OLED 화면이 계속 꺼져 있음 (로그에 `No SSD1306 found` 표시) | SDA/SCL이 I2C1 핀(GPIO4/GPIO5)에 제대로 연결됐는지 확인 — LIS3DH의 I2C0 핀(GPIO8/GPIO9)과 헷갈리기 쉬움. 배선이 맞다면 모듈의 VCC가 3.3V인지(5V 전용 모듈이면 동작 안 함), 풀업 저항 필요 여부 확인 |
| OLED은 켜지는데 글자가 깨지거나 위치가 이상함 | 모듈이 128x64가 아니라 128x32인 경우 - `oled_status.c`의 `OLED_HEIGHT`를 32로, `0xA8` 뒤의 mux ratio 값을 `0x1F`로 바꿔야 함 |
| 학습(fit)까지는 잘 되고 threshold도 출력되는데, `converter.convert()`에서 `TypeError: 'NoneType' object is not callable` (`keras_deps.get_call_context_function()().enter(...)` 부분) | 최신 TensorFlow(2.16+)의 기본 Keras가 **Keras 3**으로 바뀌면서, `tf.lite.TFLiteConverter`가 내부적으로 기대하는 Keras 2 방식의 호출 컨텍스트 API가 없어서 생기는 TensorFlow 자체의 알려진 호환성 문제입니다 — 학습은 순수 Keras 동작이라 문제없이 되고, TFLite 변환 단계에서만 발생합니다. `requirements.txt`에 `tf_keras`(레거시 Keras 2 호환 패키지)를 추가하고, `train_autoencoder.py` 맨 위에서 `import tensorflow`보다 먼저 `os.environ["TF_USE_LEGACY_KERAS"] = "1"`을 설정해서 TFLite 변환이 항상 Keras 2 호환 경로를 쓰도록 이미 반영해뒀습니다(`pip install -r requirements.txt`로 `tf_keras`까지 새로 설치한 뒤 재실행). 만약 이 환경이 이 랩 전용이 아니라 다른 프로젝트와 공유하는 venv라면(예: 다른 프로젝트의 `.venv`를 그대로 활성화해서 씀), tensorflow/keras 버전이 서로 꼬여 있을 수 있으니 위 Step 2 안내대로 이 랩 전용 venv를 새로 만드는 것도 함께 권장 |
| `pip install -r requirements.txt` 실행 후 `ERROR: pip's dependency resolver ... requires protobuf<6.0dev... but you have protobuf 7.x` | **설치 자체는 성공한 상태**입니다 — 이 줄은 설치 실패가 아니라, PC에 이미 깔려 있던 다른 패키지(예: `grpcio-tools`, 다른 프로젝트에서 설치된 것)가 원하는 protobuf 버전과, 방금 tensorflow가 새로 설치한 protobuf 버전이 서로 다르다는 pip의 사후 경고입니다. `requirements.txt`(numpy, tensorflow)는 grpcio-tools를 쓰지 않으므로 이 랩에는 영향이 없습니다. `python -c "import tensorflow as tf; print(tf.__version__)"`가 에러 없이 버전을 출력하면 그대로 `python train_autoencoder.py`를 진행하면 됩니다. 경고 자체를 없애고 싶다면 위 Step 2 안내처럼 이 랩 전용 가상환경(venv)에서 설치하세요 — 다른 프로젝트의 패키지와 격리되어 충돌이 애초에 발생하지 않습니다 |

## 다음 순서

이 트랙은 00번(개발환경 셋업) 이후로 01/02가 서로 독립적입니다 — `02_WAKE_WORD_LAB`(웨이크워드 감지, I2S 마이크 필요)을 이미 하셨어도, 아직 안 하셨어도 이 실습을 마치는 데는 상관없습니다. 아직 안 해보셨다면 이어서 진행하셔도 됩니다.
