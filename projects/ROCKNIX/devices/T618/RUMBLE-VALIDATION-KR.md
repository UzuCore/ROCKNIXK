# RG405V 진동 검증

검증일: 2026-10-08. ROCKNIX / Linux 7.1.2-rocknixk-t618.

## 확인 결과

- InputPlumber가 RG405V Buttons, RG405V Analog, `sc27xx:vibrator`를 하나의 컨트롤러로 결합한다.
- 가상 DualSense 컨트롤러에서 force feedback을 지원한다.
- InputPlumber Rumble 0.7을 500ms씩 두 번 보내고 Stop으로 종료했다. 사용자가 실제 진동을 확인했다.
- PCSX-ReARMed로 철권 3를 실행했다. 자동 remap 적용, 포트 1/2 DualShock, `GET_RUMBLE_INTERFACE`, udev force feedback 지원을 로그로 확인했다.
- 사용자가 철권 3의 게임 진동 정상 작동을 확인했다.

## 기본 설정

Odin의 `e52f663587` 변경은 이미 T618 소스에도 포함되어 있다.

- `projects/ROCKNIX/packages/emulators/libretro/retroarch/PCSX-ReARMed.rmp`: 포트 1/2 `input_libretro_device=517` (DualShock).
- RetroArch package가 기본 remap을 `/usr/config/retroarch/PCSX-ReARMed.rmp`에 설치한다.
- `setsettings.sh`가 pcsx_rearmed / pcsx_rearmed32 실행 시 remap이 없으면 `/storage/remappings/PCSX-ReARMed/PCSX-ReARMed.rmp`에 복사한다. 기존 사용자 remap은 보존한다.
- T618 InputPlumber 프로필에는 `sc27xx:vibrator`가 이미 포함되어 있다.

이번 검증을 위해 진동 코드를 추가하거나 커널을 변경하지 않았다.

## 검증 범위와 후속 항목

- 실기기 배포본에 `/usr/bin/retroarch32`가 없어 설치된 64비트 PCSX-ReARMed로 검증했다. 기기 설정은 `psx.emulator=retroarch`, `psx.core=pcsx_rearmed`다.
- 다음 배포본에서는 기본 pcsx_rearmed32에 필요한 32비트 frontend 포함 여부를 확인해야 한다.
- 메탈기어 솔리드 Disc 1은 기기에 복사했지만 게임 진동은 아직 검증하지 않았다.
- RG405M도 현재 이미지 적용 후 에뮬레이터에서 실제 진동이 정상이라고 사용자가 확인했다. 별도 시험 펄스는 느껴지지 않았지만, 이를 기기 전체의 진동 실패로 판단했던 결론은 정정한다. 별도 시험과 게임 실행은 최종 재부팅 전후로 나뉘므로 원인을 출력 방식만으로 확정하지 않는다.
