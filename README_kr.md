# ESP32-S3 AI 실습 시리즈 (FreeRTOS/ESP-IDF 기반)

ESP32-S3 위에서 LiteRT for Microcontrollers(TFLM, 구 TensorFlow Lite Micro)를 이용한 온디바이스 AI를 **FreeRTOS 기반(ESP-IDF, PlatformIO의 `framework = espidf`)**으로 실습하는 시리즈입니다. 강의/세미나 자료로 만들어졌으며, 하드웨어로 실기 검증된 내용만 담습니다.

> 이 저장소는 **FreeRTOS/ESP-IDF 트랙**입니다. 같은 주제(AI on ESP32-S3)를 **Zephyr RTOS 기반**으로 다루는 별도의 독립된 실습 시리즈도 존재하지만, 두 트랙은 서로 다른 저장소/폴더로 완전히 분리되어 있고 서로를 전제로 하지 않습니다. 어느 쪽을 먼저 해도 상관없고, 한쪽만 해도 충분합니다. 이 저장소 안의 문서·코드는 모두 FreeRTOS/ESP-IDF 기준으로만 설명합니다.

## 이 트랙의 구성

```
05_AI/
├── 00_LITERT_TFLM_SETUP/     실습 0 — 개발환경 셋업 (필수 선행)
├── 01_SENSOR_ANOMALY_LAB/    실습 1 — LIS3DH 가속도 센서 이상 탐지 (Autoencoder)
└── 02_WAKE_WORD_LAB/         실습 2 — INMP441 마이크 웨이크워드 인식
```

각 실습 폴더는 다음과 같은 공통 구조를 가집니다.

```
NN_LAB_NAME/
├── doc/    한국어(*_KR.md) / 영어(*_EN.md) 설명 문서
└── lab/    실습 코드 (PlatformIO 프로젝트)
```

## 실습 순서 — 00은 선행, 01/02는 서로 독립

- **`00_LITERT_TFLM_SETUP`은 반드시 가장 먼저 진행**하세요. 이 실습에서 확인하는 개발환경(PlatformIO `framework = espidf`, `esp-tflite-micro` 컴포넌트 등)을 이후 모든 실습이 그대로 재사용합니다.
- 00번을 마쳤다면, **`01_SENSOR_ANOMALY_LAB`과 `02_WAKE_WORD_LAB`은 서로 완전히 독립적인 실습**입니다. 어떤 순서로 진행하든, 혹은 그중 하나만 진행해도 상관없습니다. 각 실습 문서는 다른 실습을 이미 했다고 가정하지 않고, 필요한 설명을 각자 문서 안에 전부 포함하고 있습니다.
- 각 실습은 필요한 센서/마이크 하드웨어가 다릅니다. 아래 표를 참고해서 갖고 계신 하드웨어에 맞는 실습부터 시작하시면 됩니다.

## 실습별 요약 및 검증 상태

| 실습 | 주제 | 필요 하드웨어(00 공통 보드 외 추가분) | 상태 |
| --- | --- | --- | --- |
| [`00_LITERT_TFLM_SETUP`](00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md) | ESP-IDF + esp-tflite-micro 개발환경 셋업, Hello World로 환경 검증 | 없음 (보드만) | ✅ 실기 검증 완료 |
| [`01_SENSOR_ANOMALY_LAB`](01_SENSOR_ANOMALY_LAB/doc/01_SENSOR_ANOMALY_LAB_KR.md) | HW-664(LIS3DH) 가속도 센서로 Autoencoder 기반 이상 탐지, SSD1306 상태 표시(선택) | HW-664(LIS3DH), SSD1306 OLED(선택) | ✅ 실기 검증 완료 |
| [`02_WAKE_WORD_LAB`](02_WAKE_WORD_LAB/doc/02_WAKE_WORD_LAB_KR.md) | INMP441 I2S 마이크로 웨이크워드 인식 (Step 1: micro_speech yes/no, Step 2: Edge Impulse 커스텀 웨이크워드) | INMP441 (또는 호환 I2S 마이크) | ⚠️ 문서/코드 작성 완료, 아직 실기 미검증 |

공통 보드: **ESP32-S3-DevKitC-1 N16R8** (16 MB Quad Flash, 8 MB Octal PSRAM) 기준으로 모든 실습을 작성했습니다.

## 시작하기

1. **하드웨어**: ESP32-S3-DevKitC-1 N16R8 보드와 데이터 통신이 되는 USB 케이블을 준비하세요. 실습별 추가 하드웨어는 위 표를 참고하세요.
2. **개발환경**: [`00_LITERT_TFLM_SETUP`의 문서](00_LITERT_TFLM_SETUP/doc/00_LITERT_TFLM_SETUP_KR.md)를 따라 VS Code + PlatformIO(`framework = espidf`)를 먼저 준비하고, 이 실습 자체를 실기로 검증하세요.
3. **원하는 실습 선택**: 00번을 마쳤다면, 갖고 계신 하드웨어에 맞는 실습(01 또는 02)의 `doc/` 폴더에 있는 한국어 문서를 열어 따라가시면 됩니다. 각 문서는 준비물, 배선, 빌드/업로드 방법, 검증 체크리스트, 트러블슈팅까지 모두 포함합니다.
4. 각 실습의 `lab/` 폴더에는 실제 PlatformIO 프로젝트 소스가 들어 있습니다. 코드 내 주석은 모두 영어로 작성되어 있습니다.

## 문서 언어 안내

- 한국어 문서: `*_KR.md`
- 영어 문서: `*_EN.md` — 해당 실습이 실기 검증까지 완료된 이후에만 함께 제공됩니다. (`02_WAKE_WORD_LAB`은 아직 실기 미검증이라 현재 영문 문서가 없습니다.)

## 빌드 산출물 관련 안내

각 실습 폴더의 `.gitignore`가 `.pio/`, `build/`, `managed_components/`, `sdkconfig*`(보드별로 생성되는 `sdkconfig.<board>` 변형 포함)를 전부 제외하도록 되어 있습니다. `managed_components/`는 PlatformIO가 빌드할 때 IDF Component Manager로 인터넷에서 자동으로 받아오는 의존성(`esp-tflite-micro`, `esp-nn` 등)이라 저장소에 포함하지 않습니다 — 처음 빌드할 때 자동으로 채워집니다.
