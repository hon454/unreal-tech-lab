# TechLab / unreal-tech-lab

Unreal Engine 기술을 직접 구현하고 측정하는 실행형 포트폴리오.
원칙은 **Before → Measure → Optimize → Measure Again**이다.

각 데모는 Content Browser에서 해당 맵을 직접 열어 실행한다. 기본 시작 맵 `/Game/Maps/L_Welcome`은 프로젝트와 맵 실행 방법을 안내한다.

공용 전시 공간 에셋은 Epic Games의 UE 5.8 Content Examples에서 마이그레이션한 `Content/Global/DemoRoom`과 `Content/Global/Blueprints/Interactive` 및 참조 의존성을 사용한다. 원본 경로를 유지하며, 기존 TechLab 캐릭터와 중복되는 에셋은 덮어쓰지 않았다. Interactive 에셋을 가져오는 것과 각 데모의 입력·멀티플레이 동작에 연결하는 것은 별도 작업이다.

## Actor Network Dormancy

- 데모: `/Game/Maps/Networking/L_Networking_Dormancy`
- `Scripts/StartDormancyLab.ps1`: 별도 서버와 두 클라이언트 실행. 모든 클라이언트가 공격과 실험 제어를 할 수 있다.
- 안내실 → 비교 전시실 하나(네 가지 전시) → 벤치마크실로 이어진다. 소개실과 전시실은 Width 16 / Height 9, Enclosed Left를 사용하고 네 비교 전시는 한쪽 벽에 배치한다. Content Examples처럼 BP_DemoRoom의 Rooms 배열로 구성하며, 벤치마크 방은 Length 7 / Width 63 / Height 9의 개방형 설정이다.
- `F`로 비교 전시를 공격한다. `/` HUD에서 정책·자원 수·피해 비율·주기를 설정하고 APPLY, START, COMPARE, STOP, CLEAR FIELD와 결과 열람을 사용한다. 실행 중 설정은 고정되며, 종료하면 캐릭터 조작으로 돌아온다. 전시와 Initial 상태는 HUD의 전체 맵 재시작으로 복원한다.
- 완료된 필드가 있을 때 설정을 바꾸면 해당 필드를 먼저 정리한다. 이전 실행 결과는 결과 탭에 남는다.
- 이전 색상 데모의 기록(현재 자원 데모 측정 결과가 아님): [데모 사용·측정 방법](Docs/Networking/DormancyDemo.md), [실제 측정 결과](Docs/Networking/Measurements/Results.md), [측정 환경](Docs/Networking/Measurements/Environment.md).

자동 RPC/복제 검증과 실제 이동·카메라·HUD 수동 검증은 구분한다. 미측정·미검증 조건은 결과 문서에 명시한다.

[C++ 코드 배치와 클래스 책임](Docs/CodeOrganization.md): 데모 전용 코드는 `Source/TechLab/Demos/<분야>/<데모>/`에 둔다.

자원 데모의 기존 측정 결과와 실제 Insights 캡처는 `Content/DormancyResults/resource-*`에 보관하며 HUD 결과 탭에서 확인한다. 이 수치는 **공간 재구성 이전 배치**에서 측정한 기록이다. 새 배치의 성능 수치는 미측정이다. 원본 trace와 로그는 `Saved/DormancyRuns/resource-final-*`, 서버 집계는 `Saved/Dormancy/<Run ID>`에 있다. `Scripts/AnalyzeDormancy.py`에 실행 폴더 이름을 전달하면 다시 집계한다. 이전 색상 데모의 결과도 별도 기록으로 보존한다. 맵 제작·검사용 임시 스크립트는 Git에서 제외된 `Saved` 안에서만 사용한다.
