# C++ 코드 배치

TechLab은 현재 하나의 런타임 모듈을 유지한다. 데모마다 별도 모듈이나 플러그인을 만들지 않고, `Source/TechLab/Demos/<분야>/<데모>/`에 데모 전용 코드를 모은다. 공용 Third Person 캐릭터·PlayerController·GameMode와 허브 포털은 기존 위치를 유지한다. 다른 데모를 추가할 때 해당 데모의 폴더를 만들고, 데모끼리 구현 헤더를 참조하지 않는다.

## Dormancy의 책임

| 클래스 | 위치 / 책임 |
|---|---|
| `ATechLabPlayerController` | 공용 루트. 기존 입력 매핑, 컴포넌트 생성·키 바인딩, 맵 로딩 알림 전달, 서버의 메인 참가자 역할 유지 |
| `UDormancyClientComponent` | `Demos/Networking/Dormancy/`. 소유 연결의 서버 RPC, 응답, 클라이언트 최종 상태 보고, 동일 RPC를 사용하는 명령행 자동 검증 |
| `SDormancyHUD` | 같은 데모 폴더. Slate 표시, 설정·결과 탭, 이미지 확대·스크롤, 패널 개폐와 로컬 입력 모드 |
| `ADormancyLab` | 같은 데모 폴더. 서버의 요청 검증, 실험 일정·seed·구간, 액터 생성, 계측·결과 저장 |
| `ADormancySample` | 같은 데모 폴더. 복제 상태와 색상 표시. Tick·물리 없음 |
| `FDormancySettings` | 같은 데모 폴더. HUD·RPC·서버가 공유하는 작은 설정 값 타입 |

짧은 인라인 함수를 제외한 멤버 함수 구현은 그 클래스의 `.cpp`에 둔다. `TechLabPlayerController`의 함수를 `DormancyHUD.cpp`에 나누어 구현하던 구조는 제거한다. 독립된 Actor/UObject 클래스도 각각 헤더와 구현 파일을 갖는다. UI는 서버 상태를 직접 변경하지 않고 컴포넌트의 요청 경로를 사용한다.

컴포넌트는 PlayerController의 replicated default subobject다. 따라서 서버 RPC는 해당 클라이언트가 소유한 연결을 사용하고, 기존 서버 권한 검사는 `ADormancyLab`에서 유지한다. 허브와 데모맵에서 같은 패널을 사용하므로 데모별 PlayerController 교체는 도입하지 않는다. HUD/보고 타이머는 컴포넌트가 관리하고 EndPlay에서 정리한다.

공용 컨트롤러에는 현재 데모 컴포넌트를 생성·연결하는 의존성이 남는다. 이는 의도한 최소 통합 지점이다. 여러 데모가 실제로 추가되어 이 지점이 반복될 때 공통 활성화 인터페이스를 검토한다. 현재는 범용 데모 레지스트리나 기반 프레임워크를 만들지 않는다.

폴더 이동은 C++ 소스 배치 변경이다. 모듈과 reflected 클래스 이름을 유지하므로 `/Script/TechLab.DormancyLab` 같은 에셋 참조는 바뀌지 않는다. UObject 이름 변경이나 모듈 이동을 할 때는 별도로 Core Redirect와 에셋 검증이 필요하다.

## 수정 범위와 검증

AGENTS.md의 중괄호, 다중 줄 본문, 한 줄 한 문장, 명시적 타입, 리플렉션 매크로 분리, 탭 들여쓰기 규칙은 이번 Dormancy 코드와 수정한 공용 파일에 적용한다. 무관한 기존 캐릭터 코드의 서식을 일괄 변경하지 않는다.

`Source/TechLab/.clang-format`은 기본 서식 보조 설정이다(clang-format 21 기준). 타입 선택·클래스 책임·블록 뒤 빈 줄 같은 규칙 전체를 자동으로 보장하지 않으므로 AGENTS.md와 함께 검토한다. 서버의 실험 상태와 액터의 복제 상태는 private으로 두고, UI에는 읽기 API를 제공한다.

각 접근 구역은 함수 다음 멤버 변수 순서로 정리하고, 함수는 생성자·소멸자, 오버라이드, 일반 함수 순으로 둔다. 역할별 묶음과 RPC 묶음을 분리한다. 매크로가 붙은 선언 단위 사이에는 빈 줄 하나를 두되 접근 제어자 직후와 클래스 끝에는 추가하지 않는다.

오버라이드는 상속 경로에서 가장 가까운 부모 선언의 접근 수준을 유지한다. 설치된 엔진 헤더를 기준으로 확인한다. 예를 들어 Actor의 BeginPlay/EndPlay는 protected지만 ActorComponent의 동일 함수는 public이며, Character의 SetupPlayerInputComponent는 public이다.

함수를 추가할 때 부모의 동명 선언과 구현을 확인한다. 의도한 재정의에는 `override`를 붙이고 필요한 부모 동작을 `Super` 호출로 보존한다. 생략할 때는 이유를 코드에 남긴다. 실험 데이터 초기화는 레벨 재시작용 `AActor::Reset()`과 별개이므로 `ResetExperiment()`를 사용하며, 엔진의 Reset/K2_OnReset 경로는 상속한 동작을 유지한다.

구조 변경 후 Editor 빌드와 별도 서버·클라이언트의 복제/권한/맵 이동 자동 검증을 수행한다. 명령행 자동 검증은 실제 UI 클릭이나 이동·카메라 검증을 대체하지 않는다. 실제 조작 확인 전에는 미검증 상태를 유지한다.
