# 00. LiteRT(TFLM) 개발환경 셋업 — ESP32-S3

## 이 실습의 목표

ESP32-S3-DevKitC-1(N16R8) 위에서 온디바이스 AI 추론(TinyML)을 돌리기 위한 개발환경을 처음부터 구축하고, 아주 작은 예제 모델(사인 함수 근사)이 실제로 보드에서 추론에 성공하는 것까지 확인합니다. 이 실습이 끝나면 뒤에 이어지는 01(얼굴 인식), 02(웨이크워드), 03(센서 이상탐지) 실습 모두를 같은 환경 위에서 진행할 수 있습니다.

Google이 TensorFlow Lite를 **LiteRT**로 리브랜딩했지만, 마이크로컨트롤러용 런타임 자체는 여전히 **TFLM(TensorFlow Lite Micro / LiteRT for Microcontrollers)**이라는 이름으로 불립니다. ESP32-S3에서는 Espressif가 공식 포팅한 **`esp-tflite-micro`**를 씁니다 — 내부적으로 S3의 벡터 명령어(SIMD)를 활용하는 `esp-nn` 가속 커널이 포함되어 있어, 순정 TFLM보다 추론 속도가 빠릅니다.

## 준비물

- ESP32-S3-DevKitC-1 (N16R8: 16MB Quad Flash + 8MB Octal PSRAM) — 기존 실습에서 쓰던 보드 그대로 사용
- USB 케이블, PC (VS Code + PlatformIO 설치되어 있다는 전제)
- 카메라/마이크는 이 실습(00번)에서는 필요 없습니다. 01/02번에서만 필요합니다.

## ⚠️ 먼저 알아두실 것 — 프레임워크 변경

지금까지의 GPIO/Wi-Fi/MQTT/OTA/FreeRTOS 실습들은 `framework = arduino`로 진행했습니다. 하지만 `esp-tflite-micro`는 **ESP-IDF 전용 컴포넌트**로 배포되고 있어서, PlatformIO의 Arduino 프레임워크와 억지로 조합하면 `array.h`를 못 찾는 등의 컴파일 에러가 흔하게 보고됩니다(Espressif 공식 GitHub 이슈로 확인된 내용).

**해결책**: 이번 AI 예제들만 새 PlatformIO 프로젝트를 만들고 `framework = espidf`로 진행합니다. VS Code + PlatformIO라는 개발 환경 자체는 동일하고, 내부 빌드 시스템만 순정 ESP-IDF를 씁니다 — "특정 컴포넌트 생태계는 그 생태계가 공식 지원하는 빌드 시스템으로 옮기는 게 가장 확실하다"는 흔한 임베디드 개발 원칙입니다.

## Step 1. 새 프로젝트 생성 (ESP-IDF 프레임워크)

1. PlatformIO Home → New Project
2. Name: `00_LITERT_TFLM_SETUP` (또는 원하는 이름)
3. Board: `Espressif ESP32-S3-DevKitC-1` 선택
4. **Framework: `Espressif IoT Development Framework`** 선택 (Arduino 아님 — 반드시 확인)
5. 생성 완료 후 프로젝트 루트의 `platformio.ini`가 아래와 같이 되어 있는지 확인합니다.

```ini
[env:esp32-s3-devkitc-1]
platform = espressif32
board = esp32-s3-devkitc-1
framework = espidf
monitor_speed = 115200
```

이 실습 폴더의 `lab/platformio.ini`에 동일한 내용이 이미 준비되어 있으니, 새로 만든 프로젝트에 그대로 덮어써도 됩니다.

## Step 2. N16R8 Flash/PSRAM 설정 (ESP-IDF 방식)

기존 Arduino 프로젝트에서는 `board_build.*` 옵션으로 Flash/PSRAM을 설정했지만, ESP-IDF 프로젝트는 **`sdkconfig.defaults` 파일**로 설정합니다. Arduino와 달리 ESP-IDF는 소스에서 직접 빌드하기 때문에 `sdkconfig`를 완전히 자유롭게 커스터마이징할 수 있습니다.

프로젝트 루트에 `sdkconfig.defaults` 파일을 만들고 아래 내용을 추가합니다 (이 실습 폴더의 `lab/sdkconfig.defaults`에 이미 준비되어 있습니다).

```
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
```

이 값은 N16R8(16MB Quad Flash + 8MB Octal PSRAM)에 맞춘 설정으로, 이전 실습의 `qio_opi` 구성(Flash는 Quad, PSRAM은 Octal)과 같은 내용을 ESP-IDF 방식으로 표현한 것입니다. TFLM 추론은 모델과 tensor arena를 위해 메모리를 꽤 쓰기 때문에, PSRAM이 제대로 인식되는지가 특히 중요합니다.

## Step 3. esp-tflite-micro 의존성 추가 (IDF Component Manager)

ESP-IDF는 `idf_component.yml`이라는 파일로 외부 컴포넌트를 선언합니다. `src/idf_component.yml` 파일을 만듭니다 (이 실습 폴더의 `lab/src/idf_component.yml`에 이미 준비되어 있습니다).

> **PlatformIO는 `main/`이 아니라 `src/`를 씁니다**: 순정 ESP-IDF(`idf.py`)는 애플리케이션 컴포넌트 폴더 이름이 항상 `main`이지만, PlatformIO는 다른 프레임워크(Arduino 등)와 통일된 구조를 위해 기본적으로 `src/` 폴더를 씁니다(`platformio.ini`에서 `src_dir`을 따로 지정하지 않는 한). 이 실습 폴더의 `lab/`은 이미 `src/` 기준으로 준비되어 있습니다.

```yaml
dependencies:
  espressif/esp-tflite-micro: "^1.3.3"
```

PlatformIO에서 빌드하면 IDF Component Manager가 자동으로 이 의존성을 인터넷에서 받아옵니다 (`managed_components/` 폴더에 다운로드됨). 이 폴더는 빌드할 때마다 새로 채워지므로 버전관리(git)에 올리지 않는 것이 일반적입니다.

## Step 4. Hello World로 환경 검증

`esp-tflite-micro`에는 사인 함수를 근사하는 아주 작은 모델로 환경이 제대로 됐는지 확인하는 `hello_world` 예제가 포함되어 있습니다. 직접 코드를 옮겨 적기보다, 아래 명령으로 예제 자체를 받아서 그대로 빌드해보는 걸 권장합니다.

```bash
# 최초 1회 빌드해서 managed_components를 받아온 뒤 실행
pio run

# managed_components 안의 예제를 프로젝트의 src/ 폴더로 복사
# (예제 저장소 안에서는 폴더명이 "main"이지만, 우리 프로젝트에서는 "src"로 받습니다)
cp -r managed_components/espressif__esp-tflite-micro/examples/hello_world/main/* src/

# 다시 빌드 & 업로드
pio run -t upload
pio device monitor
```

빌드 후 시리얼 모니터에 사인파 예측값이 500ms마다 출력되면 환경 구축이 끝난 것입니다.

```
x_value: 1.1, y_value: 1.0
x_value: 1.2, y_value: 0.9
...
```

## 확인용 체크리스트

- [ ] PlatformIO 프로젝트를 `framework = espidf`로 생성
- [ ] `sdkconfig.defaults`에 N16R8 Flash/PSRAM 설정 반영
- [ ] `src/idf_component.yml`에 `espressif/esp-tflite-micro` 의존성 추가
- [ ] `hello_world` 예제 빌드/업로드 성공, 사인파 값 출력 확인

## 트러블슈팅

| 증상 | 원인 / 해결 |
|---|---|
| `Failed to resolve component 'tflite-lib'` | `idf_component.yml` 위치나 문법 오류 — `src/` 폴더 안에 정확히 있는지 확인 (PlatformIO는 `main/`이 아니라 `src/`를 씁니다) |
| PSRAM이 인식 안 됨 | `sdkconfig.defaults`가 실제로 적용됐는지 확인 — `.pio/build/esp32-s3-devkitc-1/sdkconfig` 파일을 열어 `CONFIG_SPIRAM=y`가 있는지 확인. 없다면 `pio run -t clean` 후 재빌드 |
| `array.h` 관련 컴파일 에러 | Arduino 프레임워크로 시도하고 계신 건 아닌지 확인 — 반드시 `framework = espidf`여야 합니다 |
| `idf_component.yml` 의존성 다운로드 실패 (네트워크 에러) | PC의 인터넷 연결/방화벽 확인. 사내망이라면 프록시 설정이 필요할 수 있습니다 |

## 다음 순서

환경 구축이 끝나면 이 트랙의 나머지 두 실습은 서로 독립적이라 어떤 순서로 진행해도 됩니다.

1. `01_SENSOR_ANOMALY_LAB` — 진동/센서 이상 탐지 (HW-664 모듈/LIS3DH 3축 가속도계 필요, SSD1306 OLED는 선택사항)
2. `02_WAKE_WORD_LAB` — 웨이크워드 감지 (I2S 마이크 필요)
