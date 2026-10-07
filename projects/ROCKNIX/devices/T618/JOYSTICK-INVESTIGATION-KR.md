# RG405V 조이스틱 축 간섭 조사

- 조사 기준일: 2026-10-08
- 대상: Anbernic RG405V
- 실행 커널: `7.1.2-rocknixk-t618`
- 상태: **vddldo0 전원 유지 설정 누락 수정 완료. 재부팅 후 양쪽 스틱 8방향 축 분리 확인.**

## 이전 관측 결과 (수정 전)

ROCKNIX에서 스틱의 한 방향을 누를 때 네 ADC 축이 함께 크게 움직이는 현상이 관측됐다. 커널 드라이버 진단 출력에는 한 번의 입력에서 네 축이 모두 약 `-1800`으로 출력된 기록이 있다. 축 간섭은 InputPlumber 이전의 커널 입력 단계에서 이미 나타난다.

같은 RG405V의 GammaOS에서 왼쪽 스틱을 위로 누른 캡처에서는 ADC CH2만 약 1775–1781mV로 움직였고 CH0/CH1/CH3는 중립 범위에 머물렀다. 이 비교는 축 혼합이 모든 환경에서 항상 발생하는 것은 아님을 보여주지만, ROCKNIX에서의 원인이 커널, ADC 설정, 물리 MUX 신호 중 무엇인지는 특정하지 못한다.

## 코드 경로

공통 어댑터는 [`rg405m-analog.c`](packages/t618-input/sources/rg405m-analog.c)이며 RG405V도 이 드라이버를 사용한다.

1. 네 축을 차례로 선택한다. MUX GPIO A=`i & 1`, B=`(i >> 1) & 1`이며 선택 순서는 `00 → 10 → 01 → 11`이다.
2. 각 선택 뒤 `usleep_range(10, 20)`으로 10–20µs 대기한다.
3. IIO processed ADC 채널을 읽고, RG405V OF 호환값에서 ADC scale을 1로 설정한다.
4. RG405V 진단 출력에는 ADC mV, 축 출력, MUX GPIO readback이 기록된다.

RG405V DTS는 A/B 선택선 GPIO36/37 active-high, enable GPIO15 active-low, 전원선 GPIO19/21/22/24로 지정한다. 벤더 joypad 역어셈블리에서도 동일한 선택 순서와 10–20µs 대기가 확인됐다. 따라서 현재 소스의 선택 순서만으로 실측된 교차 입력을 설명할 수 없다.

RG405V DTS에서 사용하지 않는 `adc-power-en-gpios = GPIO16` 선언은 제거했다. 기존 실행 DTB에는 이 선언이 없었고, GPIO16 런타임 실험에서도 축 간섭은 해결되지 않았다. RG405M DTS 및 RG405M의 동작 설정은 이 정리에서 변경하지 않았다. 이 선언 제거는 조이스틱 축 간섭의 수정으로 간주하지 않는다.

## 실험에서 확인한 한계

| 확인 항목 | 관측 결과 | 해석 |
|---|---|---|
| RG405V ADC scale 1 | 출력 포화 양상은 달라졌지만 축 간섭은 남음 | 포화와 교차 입력은 별개 문제 |
| GPIO16 ADC 전원선 런타임 변경 | 전후 네 축 평균이 거의 같음 | 원인으로 입증되지 않음 |
| GPIO16 입력/출력 상태 변경 | ADC 전압 변화는 컸지만 간섭은 해소되지 않음 | 해결책이 아님 |
| 핀 sleep-pull 변경 | 왼쪽 위 입력에서 네 축이 계속 약 -1800까지 움직임 | 이 설정으로 해결되지 않음 |
| MUX 선택별 100ms 격리 캡처 | 선택 CH2는 1605–1609mV, 다른 채널도 1437–1458mV | 긴 대기 후에도 비선택 채널 상승이 남지만 발생 위치는 특정 불가 |
| GPIO input-sense 실험 | DATA readback이 중립 MUX 선택 순서를 따름 | 실제 패드 전압이나 스틱 이동 중 선택 신호를 입증하지 않음 |

Spreadtrum GPIO 드라이버의 `.get` 구현은 `SPRD_GPIO_DATA` 레지스터를 읽는다. 따라서 GPIO DATA readback은 선택 비트의 소프트웨어 상태만 보여주며 GPIO36/37 패드에 해당 전압이 실제로 도달했음을 증명하지 않는다.

## 원인 확정에 필요한 측정

오실로스코프 또는 동등한 고임피던스 계측으로 **실제 보드 GPIO36/37 MUX 패드 전압과 ADC 출력 전압을 같은 시간축에서** 측정한다. 왼쪽 스틱을 한 방향으로 누른 채 네 MUX 상태를 모두 기록하고, 같은 기기의 GammaOS에서도 같은 측정을 비교한다.

- 선택 패드가 드라이버 명령을 따라가지 않으면 GPIO/pinmux/DT 경로를 추적한다.
- 선택 패드는 정상인데 비선택 ADC 채널이 함께 변하면 보드 MUX, ADC 입력 정착, IIO 변환 경로를 추적한다.

두 경우를 구분하기 전에는 지연시간이나 축 보정을 임의로 바꾸지 않는다. 필요한 변경은 RG405V 조건으로 한정하고 RG405M 동작을 보존한다.

## 2026-10-08: 전원 설정 누락 확인 및 수정

GammaOS에서는 `vddldo0`가 2.8V로 켜져 있었지만 ROCKNIX RG405V에서는 꺼져 있었다. 정펌 DTS에는 `regulator-always-on`이 있고, 정상 RG405M DTS에도 `regulator-always-on`과 `regulator-boot-on`이 이미 있었다. RG405V DTS에서 이 설정만 누락됐다.

- 수정 전 8방향 기록: 양쪽 스틱의 위·왼쪽 방향에서 네 축이 함께 약 -1800까지 움직였다. 아래·오른쪽 방향은 해당 축이 주로 움직였다.
- 간섭 중 GPIO 기록 667개: 선택 신호의 네 상태가 나타났고 enable은 LOW, 전원 GPIO19/21/22/24는 HIGH를 유지했다. 이 기록은 아날로그 핀 전압 측정 자체를 대신하지 않는다.
- 동일 기기에서 regulator API로 **vddldo0만 켠 뒤** 왼쪽 위 입력: `[0, -1800, 0, 0]`. 다른 세 축은 모두 0, 놓은 뒤 네 축 모두 0으로 복귀했다.
- 영구 수정: RG405V DTS에 `&vddldo0 { regulator-always-on; regulator-boot-on; };` 추가. 보정값·MUX 순서·ADC 드라이버는 변경하지 않았다.
- 기존 빌드에서 RG405V DTB와 carrier만 갱신했다. DTB 의미 차이는 이 두 속성뿐이며 symbol/phandle과 USB 설정을 보존했다.
- 기기 vendor_boot_b와 SD의 RG405V DTB/carrier에 적용하고 ROCKNIX 메뉴의 재부팅 경로로 재부팅했다. 임시 진단 모듈 없이 vddldo0 enabled/2800mV, 설정 유지, 메뉴 active, 실패 service 0개를 확인했다.
- Android boot_a/vendor_boot_a, boot_b, 커널, RG405M DTB는 적용 전후 SHA256이 동일했다.

최종 V DTB SHA256: `4fd5842f153c51802fc0fa552ce21467d209adcfc2b31324f5620958cf71b3c3`.
커널 SHA256: `c2aa22c692132fb6d97a4bfb93f51d558614c4065637eddef164a36182b635c1`.
RG405M DTB SHA256: `c57d240862938aa42e68424ba0f21260cb34dfde5f19018f44571e4f213eb434`.

별도 배포 누락도 복구했다: 기기의 모듈 색인에서 rg405m_analog이 빠져 자동 로드되지 않았다. V 전용 저장소 overlay로 색인을 재생성하고, 재부팅 후 자동 로드를 확인했다. 정규 전체 image 빌드는 기존 scripts/image의 depmod 단계가 외부 모듈을 포함해야 한다.

증거 자료: `08_build/rg405v-kernel712/controller-calibration/JOYSTICK-SESSION-20261008.md`, `joystick-20261008-analysis.json`, `joystick-pad-20261008-analysis.json`, `joystick-rail-20261008-analysis.json`. 위의 과거 가설과 실패 실험은 조사 이력이다. 재부팅 후 8방향 최종 기록 55개에서 모든 방향의 해당 축 peak는 1771~1800, 다른 축의 최대 잔여값은 18이었다. 중립 물리 값은 [18,0,0,0](전체 범위의 약1%), 가상 컨트롤러는 [128,127,127,127]로 중심128에서 1단계 이내다. 기존 네 축 포화 간섭은 재현되지 않았다. 최종 증거: joystick-final-20261008-analysis.json.
