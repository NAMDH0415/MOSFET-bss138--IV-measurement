# MOSFET BSS138 I–V Measurement

STM32L432KC Nucleo 보드에서 BSS138의 게이트-소스 전압을 스윕하고, 10 kΩ 부하 저항 양단의 전압차로 드레인 전류를 계산하는 측정 펌웨어입니다. 측정 결과는 USART2를 통해 CSV 형식으로 전송합니다.

## 측정 예시

![BSS138의 실측 게이트-소스 전압과 드레인 전류 그래프](docs/bss138-measurement-example.png)

이 화면은 이전 10회 스윕 설정에서 첫 스윕을 진행하던 중 캡처한 예시입니다. 실측 게이트-소스 전압이 약 0.9~1.2 V인 구간에서 전류가 빠르게 증가하고, 드레인 전압이 0 V에 가까워지면서 전류는 2.8 V 공급과 10 kΩ 부하로 정해지는 약 280 µA에 접근합니다. 현재 펌웨어는 부팅할 때 512점 스윕을 한 번 수행합니다.

## 회로 연결

![BSS138 측정 회로의 5×7 cm 만능기판 부품면 배치도](docs/perfboard-wiring.png)

드레인 전류 경로는 `DAC2 → 10 kΩ → BSS138 드레인 → 소스 → GND`입니다.

| Nucleo 핀 | STM32 기능 | 연결 |
| --- | --- | --- |
| A4 (PA5) | DAC1 CH2 | 10 kΩ 저항의 공급 쪽에 2.8 V 설정 |
| A1 (PA1) | ADC1 IN6 | 저항의 공급 쪽 전압 측정 |
| A0 (PA0) | ADC1 IN5 | 저항의 드레인 쪽 전압 측정 |
| A3 (PA4) | DAC1 CH1 | MOSFET 게이트 전압 설정 |
| A2 (PA3) | ADC1 IN8 | MOSFET 게이트 전압 측정 |
| GND | 공통 접지 | MOSFET 소스 |

소스가 GND에 연결되므로 측정한 게이트 전압은 게이트-소스 전압 `VGS`에 해당합니다. 드레인 전류는 `ID_uA = (VSupply − VDrain) / 10000 × 1000000`으로 계산합니다. 드레인 전압은 전류에 따라 변하므로, `VGS_Measured`와 `ID_uA`로 그린 곡선은 10 kΩ 부하 조건에서의 전달 특성을 나타냅니다.

## 측정 동작

1. 부팅 후 ADC를 보정하고, 게이트 출력과 공급 출력을 각각 0 V와 2.8 V로 설정합니다.
2. 게이트 설정값을 0~3.0 V 범위에서 양 끝점을 포함해 512점으로 스윕합니다. 설정 간격은 약 5.87 mV입니다.
3. 각 점에서 DAC 설정 후 100 ms를 기다리고, 공급·드레인·게이트 전압을 각각 16회 읽어 평균을 구합니다. 전압 변환에는 기준 전압 3.3 V를 사용합니다.
4. 측정 결과를 한 줄 전송한 뒤 50 ms 대기합니다. 한 번의 스윕이 끝나면 게이트 설정을 0 V로 돌리고 측정을 종료합니다. 다시 측정하려면 보드를 리셋합니다.

측정 범위와 스윕 횟수는 [sweep_app.h](Core/Inc/sweep_app.h)에서, 측정과 CSV 전송은 [sweep_app.c](Core/Src/sweep_app.c)에서 확인할 수 있습니다.

## 빌드 및 실행

1. 저장소를 내려받아 STM32CubeIDE의 **File → Import → General → Existing Projects into Workspace**에서 프로젝트 폴더를 선택합니다.
2. **Debug** 구성으로 빌드하고 ST-LINK를 통해 보드에 펌웨어를 다운로드합니다.
3. 시리얼 터미널에서 보드의 COM 포트를 **115200 baud, 8N1, 흐름 제어 없음**으로 엽니다.
4. CSV 헤더부터 받으려면 터미널을 연 상태에서 보드를 리셋합니다.

## UART 출력

```csv
VGS_Set,VGS_Measured,VSupply,VDrain,ID_uA,time_ms,sample_id,sweep_id,point_index
```

- `VGS_Set`, `VGS_Measured`, `VSupply`, `VDrain`은 V, `ID_uA`는 µA 단위입니다.
- `time_ms`는 펌웨어 시작 후 측정 시점까지의 시간입니다.
- 현재 설정에서 `sweep_id`는 1이며, `sample_id`와 `point_index`는 0부터 511까지 증가합니다.
