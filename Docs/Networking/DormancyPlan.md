# Actor Network Dormancy — 조사 및 측정 설계

상태: 사용자와 C 방식 접이식 HUD 및 최초 클라이언트 제어 권한을 합의했다. 아래 조사 내용은 초기 기록이며, 구현/검증 현황은 DormancyDemo.md와 Measurements/Results.md를 따른다.

## 확인된 프로젝트 상태 (2026-09-27)

- 조사 시작 시 Git 작업 트리 깨끗함. 기준 커밋 `6588fdd`.
- 엔진: `G:/Epic Games/UE_5.8`, UE 5.8.3, CL 58210709.
- CPU: AMD Ryzen 9 5950X, 16 cores / 32 logical processors. 메모리·GPU 상세는 미확인.
- TechLab 에디터 PID 53840, ContentExamples 에디터 PID 49812. ContentExamples는 수정하지 않는다.
- TechLab 에디터는 L_TechLabHub를 열고 있으며 PIE 종료 상태, 화면에 All Saved 표시 확인. 변경 직전에 dirty package 목록도 엔진 API로 검사한다.
- 기존 C++: Third Person 캐릭터, Enhanced Input 컨트롤러, GameMode, 전시 포털.
- 기본 GameMode는 기존 BP_ThirdPersonGameMode. 캐릭터·입력 자산을 재사용한다.
- 현재 허브: `/Game/TechLab/Core/Maps/L_TechLabHub`.
- 이전 경로 참조: DefaultEngine.ini의 GameDefaultMap/EditorStartupMap, DefaultEditor.ini의 SimpleMapName, Saved/BuildTechLabHub.py. 바이너리 에셋 참조 검사는 아직 수행하지 않았다.
- 현재 포털은 서버 권한 검사 후 Standalone에서는 OpenLevel, 네트워크에서는 ServerTravel로 전원을 이동시킨다. 제어자 제한은 없다.
- Basic 템플릿 후보 `/Engine/Maps/Templates/Template_Default` 존재. 복제 후 World Partition 및 외부 액터 저장 여부를 검사해야 한다.
- Installed Build에는 Win64 Editor/Game 구성이 있으나 Server 구성은 없다. 패키징 Dedicated Server 지원을 전제하지 않는다. UnrealEditor `-server`와 별도 `-game` 클라이언트를 먼저 검증한다. Editor 기반 서버라는 조건을 결과에 명시한다.
- UnrealInsights.exe 및 TraceInsights 소스가 설치되어 있다. CPU 데이터 내보내기 기능을 확인하고 실제 trace에 대해 검증할 예정.

## 최초 제안 (대체됨)

권장: A — 월드 제어대에 접근하면 E 안내, E로 상세 패널 표시.

- 월드: 제어대, 실험 오브젝트 전시장, 문제/분석/결론 안내판, 허브 이동 입구.
- 패널: 시나리오·액터 수·변경 비율·주기·단발/지속 변경, 시작/정지/초기화, 결과 열람.
- 제어자 한 명만 설정·실행 요청 가능. Dedicated에서는 최초 참가자 지정, 이탈 시 서버가 후임 지정하는 안.
- 모든 요청은 소유 PlayerController의 서버 RPC를 통해 권한·거리·상태·입력 범위·요청 빈도를 검사.
- 실행 중 설정 고정. 정지와 결과 열람 허용. 다음 실행은 액터 재생성 및 seed/카운터 초기화.
- 결과는 중앙 패널 탭, 이미지 선택 시 확대 뷰와 배율/스크롤/원래 크기/닫기 제공. 관전자도 열람 가능.
- 닫기/Escape는 커서·입력 모드를 복구한다. 이동·카메라 복귀는 사용자 수동 검증.
- 맵 이동은 제어자만 요청, 서버가 검증하여 세션 전원 이동. 실험 중에는 이동을 막고 정지 안내.

사용자 결정으로 A/B를 제외했다. 최종 구성은 접이식 상시 HUD, 최초 접속 클라이언트 MAIN/두 번째 OBSERVER, 실행 중 설정 고정, 결과 탭 이미지 확대·스크롤, 메인 요청의 서버 전원 이동이다. 자동 검증은 HUD와 같은 서버 처리 경로를 쓰되 실제 UI 검증과 구분한다.

## 최소 구현 단위

Tick/AI/물리가 없는 replicated 실험 액터, 서버 실험 제어기, 기존 PlayerController의 RPC/상호작용 확장, 패널, 에디터 자산 준비 스크립트, 측정 실행/집계 도구만 추가한다.

실험 액터 상태는 Run ID + 고정 Actor ID + StateVersion + 값으로 식별한다. 클라이언트 색상은 RepNotify로 갱신하며 서버 렌더링을 측정하지 않도록 한다. 액터별 Tick이나 검증 RPC를 만들지 않는다.

## 공정한 비교

- Baseline: 모든 실험 액터 DORM_Awake.
- Dormancy 단발: 변경 전에 FlushNetDormancy, 이후 property 변경. API 호출 직후 실제 연결의 dormancy가 완료됐다고 간주하지 않는다.
- Dormancy 지속: 변경 시작 전에 SetNetDormancy(DORM_Awake), 연속 변경 동안 유지, 종료 후 DORM_DormantAll.
- Frequent Changes: 동일 액터 수·비율·seed에 대해 주기를 단계적으로 줄인다. 각 주기에서 Awake와 Dormancy를 독립 실행한다. 단발 flush와 지속 Awake 전략도 별도 Run ID로 구분한다.
- relevancy(always relevant 여부 포함), NetUpdateFrequency, adaptive update 설정, 연결 수, 배치, tick 제한, 네트워크 에뮬레이션, trace 채널을 동일하게 고정한다.
- PRNG seed와 논리 변경 step으로 대상/값을 결정한다. 느린 서버의 누락·catch-up step을 기록하고 변경 횟수가 다른 실행을 동일 조건으로 비교하지 않는다.
- 새 실행마다 액터/채널 초기 상태를 정리한다. 공개 결과는 가능하면 프로세스도 새로 시작한 반복 실행을 사용한다.

## 구간과 규모

초기 복제 → 모든 대상 클라이언트의 초기 상태 확인 → 워밍업 → 안정 → 변경 → 변경 종료/수렴 → 완료.

초기 후보: 256/1024/4096 액터, 클라이언트 1/2개. 이는 측정 설계용 후보이며 하드웨어 측정으로 최종 규모를 정한다. 워밍업 5초, 안정/변경 각 15초부터 시작하되 초기 복제 ACK 및 실제 trace에 따라 조정한다. 성능 결과는 최소 3회, 가능하면 5회 반복하고 실행 순서를 번갈아 배치한다.

## 측정 출처와 집계

|항목|근거|주의|
|---|---|---|
|Game Thread 실제 작업|Timing Insights의 스레드 이벤트 구간 합집합|프레임 벽시계 시간과 구분, sleep/wait/idle을 작업으로 합산하지 않음|
|복제 CPU|실제 trace에서 확인한 NetDriver/복제 scope|부모·자식 inclusive 시간을 중복 합산하지 않음|
|연결별 송신|Networking Insights 송신 packet 및 content|packet bytes와 actor content bits/8을 별도 표시; NIC wire bytes로 부르지 않음|
|액터/설정 Awake/Dormant 수|제어기 자체 집계|NetDormancy 설정값이며 연결별 실제 dormancy 완료 수와 다름|
|변경/wake/flush 수|자체 counter|엔진이 처리한 replication 횟수로 해석하지 않음|
|최종 상태/수렴|클라이언트 검증 응답과 서버 시각|보고 주기와 왕복 지연이 포함되는 상한 추정|

평균/p95/최대는 동일 측정 구간의 프레임별 표본으로 집계한다. 실행별 결과와 실행 간 평균/표준편차·범위를 함께 보관한다. trace 유실, 연결 수 변경, 워밍업 미완료, 변경 일정 불일치는 결과에 실패/제외 이유를 남긴다.

`cpu,frame,bookmark,counters,net` 채널과 `-NetTrace=1`을 시작점으로 검증한다. Run ID/phase bookmark, change batch/wake/flush/client validation scope를 추가한다. HUD는 2 Hz 이하; 최종 전체 상태 검증은 안정/변경 성능 구간 밖에서 수행한다. 검증과 UI 비용도 별도 scope로 구분한다.

## 정확성 검증

클라이언트별 기대 Actor ID 집합과 version/value를 확인한다. 단순 actor count나 XOR만으로 일치 판정하지 않는다. 종료 후 분할된 최종 상태 보고를 서버가 원본과 대조하며 Run ID와 대상 연결이 모두 일치해야 통과한다. 미응답/타임아웃/이탈을 성공으로 처리하지 않는다.

단발 flush, 지속 Awake, 반복 변경 후 수렴, dormant 상태에서 late join, 지연/손실 후 최종 수렴을 각각 실행한다. 늦은 접속은 별도 correctness run으로 성능 비교의 연결 수를 바꾸지 않는다. property replication은 중간 상태 전부 수신을 보장하는 이벤트 스트림이 아니다. 중간 버전 누락과 최종 불일치를 구분한다.

## 에셋 이동과 검증 순서

1. 실행 중인 TechLab 에디터의 dirty package/PIE 상태 확인.
2. Unreal Editor AssetTools/EditorAssetLibrary로 허브를 `/Game/Maps/L_TechLabHub`로 이동. 파일시스템 이동 금지.
3. Basic 템플릿에서 `/Game/Maps/Networking/L_Networking_Dormancy` 생성, WP 없음/외부 액터 없음 검증.
4. 양방향 포털 목적지, GameDefaultMap/EditorStartupMap/ServerDefaultMap/SimpleMapName/MapsToCook 및 기존 생성 도구 갱신.
5. 참조 패키지 저장, Unreal 리다이렉터 fixup, Asset Registry referencer 및 텍스트 설정 검사. dirty 자산을 덮어쓰지 않는다.
6. 두 맵 로드/Blueprint 컴파일/패키징 포함 여부 검증 후 사용자에게 실제 이동 테스트 요청.

## 결과 전시와 완료 증거

각 결과는 엔진 CL, Run ID, 시나리오/전략, 규모/seed/연결 수/주기/비율, 실행 방식, 구간 시간, trace 파일, 원본 내보내기 파일을 연결한다. 실제 Insights 화면 캡처만 분석 이미지로 사용한다. 자체 CSV 그래프를 Insights 캡처라고 표시하지 않는다.

실시간 자체 카운터와 사전 측정 결과를 별도 탭으로 명시한다. 미측정 항목은 `미측정`, 수동 미확인은 `미검증` 표시. 결과 숫자나 병목 결론은 아직 없다.

수동 확인 항목: 두 맵의 Third Person 이동/점프/카메라, 제어대 접근·E·패널 닫기 후 조작 복귀, 허브 왕복. 요청마다 맵/실행 모드/행동/기대 결과를 명시한다.

## 참고

- Epic UE 5.8 Actor Network Dormancy: https://dev.epicgames.com/documentation/unreal-engine/actor-network-dormancy-in-unreal-engine
- FlushNetDormancy API: https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/AActor/FlushNetDormancy
- Insights reference: https://dev.epicgames.com/documentation/unreal-engine/unreal-insights-reference-in-unreal-engine-5
