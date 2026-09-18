# 02. 웨이크워드(음성 키워드) 감지

## 이 실습의 목표

I2S 디지털 마이크로 오디오를 캡처해서 `esp-tflite-micro`가 제공하는 `micro_speech` 예제("yes"/"no" 인식)를 실제 배선으로 빌드/실행해보고, 이를 발판 삼아 커스텀 웨이크워드(예: "헤이 에스프")로 확장하는 방법까지 안내합니다.

이 실습은 [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md)에서 만든 ESP-IDF 개발환경이 이미 되어 있다는 전제로 진행합니다. 아직 안 하셨다면 00번을 먼저 완료해주세요.

## 준비물

- ESP32-S3-DevKitC-1 (N16R8)
- I2S 디지털 마이크 1개 (INMP441, ICS-43434 등) — 아날로그 마이크보다 노이즈에 강하고 배선이 단순해서 TinyML 음성 예제에서 표준으로 씁니다.

### 배선

| 마이크 핀 | ESP32-S3 GPIO | 역할 |
|---|---|---|
| WS (LRCL) | GPIO15 | Word Select (LRCLK) |
| SCK (BCLK) | GPIO16 | Bit Clock |
| SD (DOUT) | GPIO17 | Serial Data (마이크 → 보드) |
| L/R | GND | 모노, 왼쪽 채널 고정 |
| VDD | 3.3V | 전원 |
| GND | GND | 접지 |

GPIO15/16/17은 스트래핑 핀(GPIO0/3/45/46)이나 USB-JTAG 핀(GPIO19/20)과 겹치지 않는, 이 시리즈에서 안전하게 쓸 수 있는 핀입니다.

### 마이크는 1개(모노)면 충분합니다

INMP441 같은 I2S 디지털 마이크는 스테레오 I2S 버스에서 자신이 어느 타임슬롯에 데이터를 실을지를 **L/R 핀 하나로 선택**합니다 — L/R을 GND에 연결하면 Left 채널, VDD(3.3V)에 연결하면 Right 채널로 동작합니다. 즉 L/R 핀은 "마이크를 2개 붙이라"는 뜻이 아니라, "이 마이크 하나가 왼쪽/오른쪽 중 어디에 응답할지"를 정하는 핀입니다. 이 랩은 L/R을 GND에 고정해서 **마이크 1개, Left 채널 고정**으로 사용합니다.

마이크를 2개(L+R 스테레오) 붙이지 않는 이유는 다음과 같습니다.

- **모델 자체가 모노 입력으로 학습되어 있음**: `micro_speech` 예제의 사전 학습된 yes/no 모델과, Step 2에서 Edge Impulse로 직접 학습할 커스텀 웨이크워드 모델 모두 단일 채널(모노) 오디오를 입력으로 가정합니다. 스테레오로 캡처해도 결국 한쪽 채널만 쓰거나 두 채널을 믹스/평균하는 추가 전처리가 필요해, 정확도상 이득이 없습니다.
- **코드 구조가 모노 기준**: `audio_provider.cc`/`i2s_setup.cc`는 I2S를 모노 슬롯 모드로 설정하고 단일 채널 샘플을 링버퍼에 채우는 구조입니다. 스테레오로 바꾸려면 I2S 설정 변경뿐 아니라, 인터리빙된 L/R 프레임에서 채널을 분리하는 코드를 새로 작성해야 합니다.
- **2마이크가 필요한 상황과는 목적이 다름**: 마이크 2개(또는 그 이상의 배열)는 보통 소리가 어느 방향에서 왔는지 추정하는 DOA(Direction of Arrival)나 노이즈 방향을 걸러내는 빔포밍(beamforming)처럼, "어디서 들리는가"가 중요한 응용에서 씁니다. 이 랩의 목표는 "어떤 단어가 들렸는가"를 판정하는 키워드 스포팅(keyword spotting)이라, 방향 정보가 필요 없어 1개로 충분합니다.

정리하면, INMP441 1개 + L/R을 GND에 연결하는 배선(위 배선표)이 이 랩에 맞는 구성입니다. 스테레오 구성은 이후 방향 추정 같은 별도 실습을 하고 싶을 때 고려하시면 됩니다.

## 정직하게 말씀드릴 부분

`esp-tflite-micro`에 포함된 `micro_speech` 예제는 **"yes"/"no" 단 두 단어만** 인식하는 작은 데모 모델입니다. 이게 "웨이크워드 감지"의 기본 파이프라인(오디오 캡처 → 특징 추출(MFCC 유사) → 신경망 추론 → 판정)을 보여주는 표준 예제이긴 하지만, **"헤이 XX" 같은 커스텀 웨이크워드를 인식하려면 직접 모델을 학습**시켜야 합니다. 이 문서는 먼저 기본 예제로 파이프라인을 검증한 뒤(Step 1), 커스텀 웨이크워드로 확장하는 경로(Step 2)까지 안내합니다.

## 폴더 구조

이 랩은 두 단계로 나뉘며, 각 단계가 독립된 PlatformIO 프로젝트입니다 (`01_SENSOR_ANOMALY_LAB`이 `01_data_collect`/`02_train_autoencoder`/`03_inference`로 나뉜 것과 같은 방식).

```
02_WAKE_WORD_LAB/
├── doc/
│   └── 02_WAKE_WORD_LAB_KR.md   (이 문서)
└── lab/
    ├── 01_yesno_demo/           Step 1 — micro_speech(yes/no) 예제
    └── 02_custom_wakeword/      Step 2 — Edge Impulse 커스텀 웨이크워드
```

Step 1과 Step 2는 별개의 `pio run` 대상입니다 — 각 단계로 들어갈 때마다 그 하위 폴더에서 (`cd lab/01_yesno_demo` 또는 `cd lab/02_custom_wakeword`) 빌드/업로드 명령을 실행합니다.

## Step 1. micro_speech 예제로 파이프라인 검증

`lab/01_yesno_demo/`에 이 예제용 PlatformIO ESP-IDF 프로젝트가 준비되어 있습니다. `src/idf_component.yml`(esp-tflite-micro 의존성 선언)과 `src/CMakeLists.txt`(소스 파일 목록)는 **이미 완성되어 있으므로 그대로 둡니다** — 아래에서 받아올 예제 소스 코드만 채워 넣으면 됩니다. (PlatformIO는 순정 ESP-IDF의 `main/` 대신 `src/`를 기본 소스 폴더로 씁니다 — 자세한 내용은 [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md) 참고.)

```bash
cd lab/01_yesno_demo
pio run   # managed_components/에 esp-tflite-micro를 내려받기 위한 1차 빌드 (에러 나도 정상)
```

다운로드된 예제 소스에서 **오디오/모델 관련 소스 파일만** `src/`로 복사합니다 (예제 저장소 안에서는 폴더명이 `main`이지만, 우리 프로젝트에서는 `src`로 받습니다. 예제 자체의 `idf_component.yml`은 로컬 개발용 상대경로를 가리키고 있어 우리 프로젝트에서는 쓸 수 없으므로 복사하지 않습니다 — 이미 우리 `src/idf_component.yml`이 올바른 버전을 선언하고 있습니다).

```bash
SRC=managed_components/espressif__esp-tflite-micro/examples/micro_speech/main
cp $SRC/main.cc $SRC/main_functions.cc $SRC/main_functions.h \
   $SRC/audio_provider.cc $SRC/audio_provider.h \
   $SRC/feature_provider.cc $SRC/feature_provider.h \
   $SRC/no_micro_features_data.cc $SRC/no_micro_features_data.h \
   $SRC/yes_micro_features_data.cc $SRC/yes_micro_features_data.h \
   $SRC/model.cc $SRC/model.h \
   $SRC/recognize_commands.cc $SRC/recognize_commands.h \
   $SRC/command_responder.cc $SRC/command_responder.h \
   $SRC/micro_features_generator.cc $SRC/micro_features_generator.h \
   $SRC/micro_model_settings.h \
   $SRC/audio_preprocessor_int8_model_data.h \
   $SRC/ringbuf.c $SRC/ringbuf.h \
   $SRC/i2s_setup.cc $SRC/i2s_setup.h \
   src/
```

### 마이크 핀을 실제 배선에 맞게 수정

`audio_provider.cc`는 기본적으로 ESP32-S3용 기본 핀(SCLK=GPIO41, LRCLK=GPIO42, SDOUT=GPIO2)으로 마이크를 초기화합니다. 이 랩의 배선(위 배선표)에 맞게 아래 한 줄만 수정합니다.

```bash
grep -n "static Microphone mic;" src/audio_provider.cc
```

찾은 줄을,

```cpp
static Microphone mic;
```

아래처럼 바꿉니다 (생성자 인자 순서는 `Microphone(sclk, lrclk, sdo)`입니다 — `src/i2s_setup.h`의 클래스 선언 참고).

```cpp
static Microphone mic(GPIO_NUM_16, GPIO_NUM_15, GPIO_NUM_17);  // SCK, WS, SD
```

### 빌드 & 실행

```bash
pio run -t upload
pio device monitor
```

마이크에 대고 "yes" 또는 "no"라고 말하면, 시리얼 모니터에 아래와 같은 형식으로 감지 결과와 확신도(score, 0~1 사이)가 출력됩니다.

```
Heard yes (0.8734) @1234ms
Heard no (0.9012) @2156ms
```

## Step 2. 커스텀 웨이크워드로 확장 — Edge Impulse

Step 1의 `yes`/`no` 모델은 esp-tflite-micro가 미리 학습시켜서 제공하는 데모용 모델이라, 우리가 직접 바꿀 수 없습니다. 원하는 단어(예: "헤이 에스프")를 인식시키려면 그 단어로 **직접 모델을 학습**시켜야 하는데, 이를 위한 데이터 녹음/전처리/CNN 설계/학습을 전부 손으로 하는 건 상당한 작업입니다. 이 Step에서는 그 과정을 **Edge Impulse**(웹 기반 TinyML 학습 플랫폼, 무료 플랜으로 개인 프로젝트에 충분)로 크게 단축하고, 학습된 모델을 실제 ESP32-S3 보드에서 마이크로 실시간 인식하는 예제(`lab/02_custom_wakeword/`)까지 만들어봅니다.

Step 1과 Step 2는 마이크 배선(위 배선표)을 그대로 재사용하되, **완전히 별개의 PlatformIO 프로젝트**입니다 — Edge Impulse가 내보내는 SDK가 자체 TensorFlow Lite Micro 사본을 포함하고 있어서, esp-tflite-micro와 한 프로젝트에 섞으면 버전이 충돌할 수 있기 때문입니다. 그래서 Step 1의 오디오 캡처 코드를 그대로 갖다 쓰는 게 아니라, 같은 I2S 설정 방식을 `lab/02_custom_wakeword/src/main.cc`에 새로 작성해뒀습니다.

### 2-1. Edge Impulse 프로젝트 생성 및 데이터 수집

1. [edgeimpulse.com](https://edgeimpulse.com)에서 무료 계정을 만들고, 새 프로젝트를 생성합니다 (프로젝트 유형은 "Audio" 계열 템플릿이 있으면 선택, 없으면 그냥 빈 프로젝트로 시작해도 됩니다).
2. **데이터 수집(Data acquisition) 탭**으로 이동합니다. 데이터를 넣는 방법은 두 가지입니다.
   - **스마트폰/PC 브라우저로 녹음** (가장 간단): "Data acquisition" 탭의 "Connect a new device"에서 QR코드를 스캔하면 스마트폰 브라우저가 녹음 장치로 연결됩니다. 별도 펌웨어 작업 없이 바로 시작할 수 있어서 처음에는 이 방법을 권장합니다.
   - **ESP32-S3 보드로 직접 녹음**: [Edge Impulse CLI](https://docs.edgeimpulse.com/docs/edge-impulse-cli/cli-installation)의 `edge-impulse-data-forwarder`를 쓰면 보드의 실제 마이크로 녹음한 데이터를 바로 업로드할 수 있습니다. 이 방법은 이 랩의 정확한 마이크/배선 환경을 그대로 반영한다는 장점이 있지만, 별도 CLI 설치와 설정이 필요해 처음에는 건너뛰어도 됩니다.
3. 다음 세 종류의 라벨(카테고리)로 데이터를 모읍니다.
   - **웨이크워드 라벨** (예: `hey_esp`): 원하는 단어를 짧게(1초 내외) 끊어서 **최소 50회 이상** 녹음합니다. 말하는 속도/억양/거리를 조금씩 다르게 하는 것이 정확도에 도움이 됩니다.
   - **`noise`**: 조용한 배경 소음, 생활 소음(TV, 대화, 타이핑 등)을 녹음합니다. 실제로 이 기기를 쓸 환경의 소리를 최대한 많이 포함시키는 게 오탐(false positive) 방지에 정확도보다 더 중요할 때가 많습니다.
   - **`unknown`**: 웨이크워드가 아닌 다른 단어/문장들을 녹음합니다. "noise"가 배경음이라면 "unknown"은 "웨이크워드는 아니지만 사람 말소리인 경우"를 학습시키는 역할입니다.
   - 세 라벨 모두 데이터 양이 어느 정도 비슷하게 균형을 맞추는 게 좋습니다 (웨이크워드만 많고 noise/unknown이 적으면 오탐이 잦아집니다).
4. 화면 우측의 "Train / Test split"에서 수집한 데이터의 약 80%를 Training, 20%를 Test로 자동 분배해줍니다 (기본값 그대로 사용하면 됩니다).

### 2-2. Impulse Design (전처리 + 모델 구성)

1. **Create impulse** 탭에서:
   - Input block: 마이크 배선표대로면 샘플링 레이트는 **16000Hz**를 권장합니다 (window size 1000ms, window increase 500ms 정도가 무난한 시작점입니다 — window increase가 곧 이 랩 코드의 슬라이스 크기와 연결됩니다).
   - Processing block: **"Audio (MFCC)"** 또는 **"Audio (MFE)"**를 추가합니다 (음성 키워드 인식에 표준적으로 쓰이는 전처리로, `micro_speech`가 내부적으로 하는 특징 추출과 목적이 같습니다).
   - Learning block: **"Classification"**을 추가합니다 (작은 CNN 구조가 기본으로 제공됩니다).
2. **MFCC/MFE 탭**에서 "Generate features"를 눌러 전처리 결과를 미리 확인합니다. 웨이크워드와 noise/unknown의 특징이 시각적으로 잘 구분되어 보이면 좋은 신호입니다.
3. **Classifier 탭**에서 기본 신경망 구조와 학습 설정(epoch, learning rate 등)을 그대로 두고 **Start training**을 누릅니다. 학습이 끝나면 정확도(accuracy)와 혼동행렬(confusion matrix)이 나옵니다 — 특정 라벨이 자꾸 다른 라벨로 오인식된다면 그 라벨의 데이터를 더 모아서 재학습하는 식으로 반복 개선합니다.
4. **Model testing 탭**에서 Test 데이터셋으로 최종 정확도를 확인합니다.

### 2-3. SDK 내보내기 및 통합

1. **Deployment 탭**으로 이동해서 **"C++ library"**를 선택하고, 하단의 최적화 옵션은 기본값(EON Compiler 사용 등)으로 두고 **Build**를 누릅니다. 빌드가 끝나면 zip 파일이 다운로드됩니다.
2. 압축을 풀면 아래와 같은 구조가 나옵니다.
   ```
   (다운로드한 zip)/
   ├── edge-impulse-sdk/
   ├── model-parameters/
   ├── tflite-model/
   ├── main/            ← Edge Impulse가 만든 데모용 예제 (정적 단발 추론, 마이크 캡처 없음)
   ├── CMakeLists.txt
   ├── sdkconfig, ...
   ```
3. 이 중 **`edge-impulse-sdk/`, `model-parameters/`, `tflite-model/` 세 폴더만** `lab/02_custom_wakeword/src/`로 복사합니다. `main/`이나 최상위 `CMakeLists.txt`, `sdkconfig` 등은 복사하지 않습니다 — 이 프로젝트에는 이미 PlatformIO `src/` 구조에 맞춘 `CMakeLists.txt`와, **마이크로 실시간 연속 추론**을 하는 `main.cc`가 준비되어 있습니다 (Edge Impulse가 기본 제공하는 `main/main.cpp`는 코드에 미리 박아둔 고정 숫자 배열 하나를 한 번만 추론해보는 데모라서, 실제 마이크 입력을 받는 이 랩의 목적에는 맞지 않습니다).
   ```bash
   cd (다운로드한 zip 압축 해제 폴더)
   cp -r edge-impulse-sdk model-parameters tflite-model \
       <저장소 경로>/02_WAKE_WORD_LAB/lab/02_custom_wakeword/src/
   ```
4. `lab/02_custom_wakeword/src/main.cc` 상단의 `SAMPLE_RATE_HZ`(기본 16000)가 2-2에서 설정한 Input block의 샘플링 레이트와 같은지 확인합니다. 다르면 여기서 실제 값으로 맞춰줍니다.
5. `DETECTION_THRESHOLD`(기본 0.8)와 `is_background_label()`에 나열된 라벨 이름(`noise`, `unknown`)도 실제로 2-1에서 사용한 라벨명과 일치하는지 확인합니다.

### 2-4. 빌드 & 실행

```bash
cd lab/02_custom_wakeword
pio run -t upload
pio device monitor
```

부팅 후 잠시(대략 1초 내외) 뒤부터 라벨별 확신도가 계속 출력되며, 웨이크워드가 임계값 이상으로 인식되면 `>>> WAKE WORD DETECTED: ...` 줄이 함께 출력됩니다.

```
Predictions (DSP 12 ms, classification 4 ms):
  hey_esp: 0.03421
  noise: 0.91204
  unknown: 0.05375
Predictions (DSP 11 ms, classification 4 ms):
  hey_esp: 0.94018
  noise: 0.02870
  unknown: 0.03112
>>> WAKE WORD DETECTED: hey_esp (0.94)
```

## 관찰 포인트

**Step 1 (micro_speech)**

- 웨이크워드 모델은 보통 "1초 분량의 오디오 슬라이딩 윈도우"를 짧은 간격(이 예제는 `kFeatureStrideMs = 20ms`)으로 계속 추론합니다 — ESP32-S3에서 이런 소형 CNN은 실시간 처리에 충분히 여유가 있습니다.
- `command_responder.cc`의 `RespondToCommand()`가 인식 결과를 출력하는 지점입니다 — 실제 제품에서는 여기서 시리얼 출력 대신 LED 점등, GPIO 트리거, MQTT 발행 등 원하는 동작으로 바꿔 끼우면 됩니다.
- 검출 후 바로 다음 검출까지 약간의 디바운스가 코드에 이미 들어있습니다(`recognize_commands.cc`의 쿨다운 로직) — 같은 발화 하나가 여러 윈도우에 걸쳐 중복 감지되는 것을 방지합니다.

**Step 2 (Edge Impulse 커스텀 웨이크워드)**

- `noise`/`unknown` 데이터를 충분히 모으지 않으면, 조용한 환경에서도 오탐(false positive)이 잦아집니다 — 실제 사용 환경의 배경 소음을 데이터셋에 포함시키는 게 정확도보다 더 중요한 경우가 많습니다(학습 데이터의 "정상/기준" 조건 자체가 한쪽으로 치우치면 모델이 실사용 환경을 오판하는, TinyML 프로젝트에서 흔히 겪는 함정입니다).
- `main.cc`는 EI SDK의 `run_classifier_continuous()`가 내부적으로 유지하는 슬라이딩 윈도우에 오디오 슬라이스를 하나씩 계속 넣어주는 구조입니다 — Step 1의 `kFeatureStrideMs`와 개념적으로 동일한 역할을 EI SDK 쪽 `EI_CLASSIFIER_SLICE_SIZE`가 담당합니다.
- 검출 후 반응(LED, GPIO, MQTT 등)을 붙이고 싶다면 `main.cc`의 `>>> WAKE WORD DETECTED` 출력 바로 그 지점이 Step 1의 `RespondToCommand()`와 같은 역할을 하는 곳입니다.

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| 계속 "unknown"만 나옴 | I2S 핀 배선/설정 오류로 오디오 자체가 안 들어오고 있을 가능성 — `audio_provider.cc`의 `CaptureSamples()`에 임시로 raw 값을 `printf`로 찍어서 마이크 신호가 실제로 들어오는지 먼저 확인 |
| yes/no 정확도가 낮음 | 마이크와 입 사이 거리, 배경 소음 확인 — 데모 모델은 조용한 환경 기준으로 학습되어 있음 |
| `undefined reference` 등 링크 에러 | `src/CMakeLists.txt`의 `SRCS` 목록과 실제로 복사한 파일 목록이 일치하는지 확인 (위 Step 1의 cp 명령과 `lab/01_yesno_demo/src/CMakeLists.txt`가 짝이 맞도록 이미 준비되어 있습니다) |
| `Failed to resolve component 'tflite-lib'` | [00_LITERT_TFLM_SETUP](../../00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md) 트러블슈팅 참고 |
| (Step 2) `edge-impulse-sdk/cmake/utils.cmake`를 찾을 수 없다는 CMake 에러 | Edge Impulse Studio에서 내보낸 `edge-impulse-sdk/`, `tflite-model/`, `model-parameters/` 폴더를 `lab/02_custom_wakeword/src/`에 아직 복사하지 않은 상태입니다 (Step 2의 "SDK 내보내기 및 통합" 참고). `src/CMakeLists.txt`에 이 상황을 감지해 안내 메시지를 띄우는 체크가 이미 들어있습니다 |
| (Step 2) 계속 "noise"/"unknown"만 나오거나 오탐이 잦음 | Studio의 학습 데이터가 부족하거나 실제 사용 환경 소음이 데이터셋에 없을 가능성 — `DETECTION_THRESHOLD`(기본 0.8)를 낮추거나, "noise"/"unknown" 데이터를 실제 사용 환경에서 더 많이 녹음해서 재학습 |
| (Step 2) 부팅 후 한동안 아무 것도 안 찍힘 | 의도된 동작입니다 — `main.cc`는 모델의 슬라이딩 윈도우가 실제 오디오로 완전히 채워질 때까지(`EI_CLASSIFIER_SLICES_PER_MODEL_WINDOW`개 슬라이스) 출력을 건너뜁니다. 보통 1초 내외이니 조금 기다려보세요 |
| (Step 2) 재부팅 반복 / 크래시 | 스택 오버플로 가능성 — `sdkconfig.defaults`의 `CONFIG_ESP_MAIN_TASK_STACK_SIZE`를 8192보다 더 키워보세요 |
| (Step 2) 인식 정확도가 낮거나 매번 다른 라벨이 나옴 | `main.cc`의 `SAMPLE_RATE_HZ`(기본 16000)가 Edge Impulse Studio에서 설정한 오디오 샘플링 레이트와 실제로 일치하는지 확인 — 다르면 에러 없이 조용히 정확도만 떨어집니다 |

## 다음 순서

이 트랙은 00번(개발환경 셋업) 이후로 01/02가 서로 독립적입니다 — `01_SENSOR_ANOMALY_LAB`(진동/센서 이상 탐지, HW-664 모듈/LIS3DH 3축 가속도계 필요, SSD1306 OLED는 선택사항)을 이미 하셨어도, 아직 안 하셨어도 이 실습을 마치는 데는 상관없습니다. 아직 안 해보셨다면 이어서 진행하셔도 됩니다.
