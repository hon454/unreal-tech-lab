# 측정 환경

- 날짜: 2026-09-27, Asia/Seoul.
- UE 5.8.3, CL 58210709, installed Editor build.
- AMD Ryzen 9 5950X, 16C/32T, 물리 메모리 63.9 GiB (UBT 보고).
- Windows 11 25H2, OS build 26200.9457 (엔진 로그).
- 서버: UnrealEditor.exe -server -nullrhi. 클라이언트: 같은 호스트의 독립 UnrealEditor.exe -game -nullrhi 프로세스 2개.
- 측정 중 캐릭터 입력을 보내지 않았다. 렌더링/GPU 작업은 측정 대상이 아니다.
- 서버/클라이언트 t.MaxFPS=60. NetUpdateFrequency/MinNetUpdateFrequency=30, AlwaysRelevant, adaptive update off.
- 연결 netspeed=100000 (실제 서버 로그). ReplicationGraph/Iris를 별도 활성화하지 않은 GameNetDriver/IpNetDriver의 기본 actor replication 경로.
- 기존 ContentExamples 에디터와 사용자 앱은 종료하지 않았다. 코어 affinity 또는 CPU 격리를 적용하지 않았으므로 스케줄링/메모리 자원 경쟁과 반복 편차가 존재한다. 프레임 실측 빈도는 runs.json의 effective_frame_hz에 보존한다.
- 반복 행렬의 모듈 SHA256: `B6792D73FA099744A51F08B68F555C7E2797D10C26B06B30AEE8002355F16A0C` (`UnrealEditor-TechLab.dll`).
- 행렬 실행 중 UI 크기/버튼 활성 및 추가 검증용 종료 대기 코드는 소스에 수정했으나 실행 바이너리를 교체하지 않았다. 행렬 완료 후 최종 빌드와 별도 정확성 검증을 수행한다.
- 실제 Insights trace와 engine scope에서만 성능 값을 얻는다. 결과 그래프/표를 Insights 스크린샷으로 위장하지 않는다.

현재 행렬은 warm 3초, stable/change 6초로 비교한다. 각 실행마다 프로세스와 액터를 새로 만들며 초기 상태 ACK를 기다린다. 더 긴 구간·다른 하드웨어·원격 네트워크·패키징 전용 서버의 성능은 미측정이다.

이후 AGENTS.md 적용을 위해 데모 소스 폴더와 클래스 책임을 분리했다. 소유 RPC는 PlayerController에서 DormancyClientComponent로 이동했고 HUD는 별도 Slate 클래스가 되었다. 기존 18회 성능 결과는 위 SHA256의 바이너리로 측정한 기록이며, 리팩터링 후 바이너리를 재측정한 결과가 아니다. `refactor-final-*` 실행은 구조 변경의 정확성 회귀 검증용으로, 기존 성능 통계에 합산하지 않는다.
