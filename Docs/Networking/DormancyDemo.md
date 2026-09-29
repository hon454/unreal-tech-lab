# Actor Network Dormancy 데모

## 사용

`Scripts/StartDormancyInteractive.ps1`은 별도 서버와 화면이 있는 두 클라이언트를 시작한다. 먼저 접속한 클라이언트가 MAIN CONTROLLER, 두 번째가 OBSERVER다. 에디터의 Play만 누른 Standalone 실행은 네트워크 성능 증거로 사용하지 않는다.

1. `/Game/Maps/L_TechLabHub`에서 `/` 키로 접이식 HUD를 연다.
2. 메인 클라이언트의 `Enter Dormancy (ALL)`로 전원 이동한다.
3. 실험 탭에서 시나리오, 액터 수, 변경 비율/주기, 단발 flush 또는 지속 Awake 전략을 선택한다. APPLY는 설정만 적용하고 START는 선택된 설정으로 독립 실행한다.
4. 서버가 Initial → Warmup → Stable → Change → Converge를 진행한다. 실행 중 설정과 이동은 잠긴다. STOP은 조기 종료 후 최종 상태 검증을 수행하며 성능 완료 실행으로 표시하지 않는다.
5. RESULTS / EVIDENCE에서 사전 측정 결과와 실제 Insights 캡처를 읽고 확대·스크롤한다. 실시간 자체 카운터와 사전 측정 수치는 별개다.
6. Collapse `/` 또는 Escape로 패널을 닫는다. Return Hub (ALL)은 서버에서 검증하여 참가자 전원을 이동시킨다.

A 제어대와 B 월드 버튼은 구현하지 않는다. 기존 허브 포털은 안내물이며, 이 데모의 이동은 HUD에서만 요청한다. Seamless travel의 컨트롤러 교체 시 메인 역할을 복사하여 지정의 연속성을 유지한다. 메인 이탈 시 남은 첫 참가자를 서버가 후임으로 지정한다.

현재 전시에는 실제 trace에서 내보낸 측정 수치/분석이 들어 있다. 실제 Insights PNG 캡처는 Computer Use의 Esc 중단으로 아직 없으며, 이미지 열람/확대 조작은 미검증이다. 가짜 이미지로 대체하지 않았다.

## 구현과 측정 경계

- `Demos/Networking/Dormancy/DormancyLab`: 공통 seed/변경 일정, 구간/계측, 서버 검증. 실험 액터는 별도 `DormancySample` 클래스다.
- `DormancyClientComponent`: PlayerController 소유 컴포넌트의 RPC, 클라이언트 상태 보고, 자동 테스트 드라이버.
- `SDormancyHUD`: 접이식 Slate HUD와 결과 표시. 공용 컨트롤러의 구현을 다른 파일에 분산하지 않는다. 자세한 배치와 책임은 [코드 구조](../CodeOrganization.md)에 기록한다.
- 실험 액터에는 Tick, AI, 물리, movement replication이 없다. 상태는 Run GUID/Actor ID/version/value이며 RepNotify가 색상을 갱신한다. custom primitive data를 사용한다.
- 30Hz NetUpdateFrequency 및 AlwaysRelevant로 고정하여 거리나 카메라가 A/B 비교에 개입하지 않게 한다. 서버 프레임 제한은 60Hz, adaptive update는 비활성화한다.
- 단발은 property 변경 전에 FlushNetDormancy. 지속은 고정 cohort를 Awake로 전환하고 변경 종료 후 DormantAll로 복귀한다. Baseline도 같은 cohort/seed/PRNG 호출 순서를 사용한다.
- Awake/Dormant 수는 설정 상태다. 연결별 dormancy ACK 완료 수가 아니다. flush는 일시적 복제를 요청하고 NetDormancy 설정값은 유지한다.
- 검증은 정렬된 Actor ID/version/value의 SHA1과 개수·중복 ID 검사를 사용한다. 해시 비교에 의한 일치이며 모든 property를 별도 전송하여 바이트 비교하는 방식은 아니다. Run GUID로 이전 실행을 배제한다. 중간 버전은 합쳐질 수 있다.
- 상태 보고는 Initial/Converge/Complete에서만 1초 이하 빈도로 생성한다. HUD 텍스트는 2Hz. 실제 복제 CPU는 Insights에서 읽으며 자체 카운터로 대체하지 않는다.
- 수렴 시간은 변경 종료부터 서버가 클라이언트 보고를 확인할 때까지의 상한 추정이며 보고 주기와 RTT를 포함한다.

## 재현 도구

`RunDormancy.ps1`: 서버 + headless 클라이언트 두 개. 서로 다른 디렉터리/Run ID로 실행하며 기존 결과 디렉터리는 덮어쓰지 않는다. 자동 드라이버는 HUD와 동일한 ServerLabAction RPC를 호출한다. 플레이어 입력을 자동화하지 않는다.

`RunDormancyMatrix.ps1`: 같은 4,096개 액터, 2개 연결, seed 74219, warm 3초, stable/change 각 6초. 저빈도 1%/1초와 고빈도 10%/0.1초·0.05초에서 Awake/Dormancy를 3회씩 교차 순서 실행한다. UI 기본 구간은 warm 5초, stable/change 각 15초다.

`ExportDormancy.ps1`: 설치된 UnrealInsights.exe의 공식 TimingInsights 내보내기를 사용한다. `AnalyzeDormancy.py`: 내보낸 프레임별 이벤트의 평균/p95/최대와 반복 실행 편차를 집계한다. 원본은 `Saved/DormancyRuns/<이름>`, 상태 로그는 `Saved/Dormancy/<Run ID>`, 요약은 `Docs/Networking/Measurements`에 있다.

trace는 cpu/frame/bookmark/counters/net/region, NetTrace=1, statnamedevents 조건으로 수집한다. 같은 호스트의 Editor 기반 -server와 -game -nullrhi 실행이다. 패키징한 전용 Server target 또는 원격 하드웨어 결과로 확대 해석하지 않는다. 동일 호스트 클라이언트와 기존 다른 앱의 자원 경쟁이 남아 있다.

## 결과 해석

GT active는 trace의 FEngineLoop::Tick에서 계측 idle과 명시적 RHI wait의 합집합을 뺀 값이다. OS의 실제 CPU 사용 시간과 같지 않으며 미계측 대기나 선점 가능성을 명시한다. 프레임 벽시계 시간도 별도로 보존한다.

복제 CPU는 엔진 ServerReplicateActors Time이다. Process Prioritized Actors/Replicate Actor/Consider Actors는 원인 설명용 하위 scope로 부모와 합산하지 않는다. 단발 flush 비용은 TimerManager 아래의 자체 mutation scope에 있으므로 복제 scope와 별도로 확인한다.

연결별 OutTotalBytes/OutTotalPackets는 엔진 관측 누계를 샘플링한 것이다. PacketBytes에는 엔진 PacketOverhead가 포함된다. 실제 NIC wire 크기나 application payload 크기로 부르지 않는다. Networking Insights의 packet/content 화면에서 연결과 실험 액터 내용을 별도로 확인한다.

## 수동 테스트 (확인 전 미검증)

|맵 / 모드|행동|기대 결과|
|---|---|---|
|Hub / 별도 서버+2 clients|양쪽에서 이동·점프·카메라|기존 Third Person 조작 정상|
|Hub / MAIN|`/` → Enter Dormancy|두 클라이언트 모두 데모맵 도착, MAIN 지정 유지|
|Dormancy / 양쪽|`/` 펼치기·접기·Escape|접은 뒤 이동·카메라 복귀|
|Dormancy / MAIN|설정 변경→START→STOP→RESET|설정 잠금, 조기 종료 검증, 초기화|
|Dormancy / OBSERVER|패널 확인|설정/실행/정지/이동 비활성, 결과 열람 가능|
|Dormancy / 양쪽|실행 중 상태 오브젝트 관찰|클라이언트 색상 변화, 최종 상태 일치|
|Dormancy / 양쪽|결과 탭 이미지 확대·스크롤|실제 캡처 확대와 스크롤, 텍스트 판독 가능|
|Dormancy / MAIN|종료 후 Return Hub|전원 허브 복귀|

자동 RPC 검증 통과는 위 UI/조작 수동 테스트 통과를 의미하지 않는다.
