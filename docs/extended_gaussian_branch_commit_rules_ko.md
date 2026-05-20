# Extended Gaussian 브랜치/커밋 규칙

## 목적

`main` 브랜치에는 항상 유효한 상태만 유지하고, 일상적인 구현/수정 작업은 `develop/<feature>` 작업 브랜치에서 진행한다.

## 브랜치 전략

- `main`
  - 배포 가능하거나 기준점으로 삼을 수 있는 안정 상태만 유지한다.
- `develop/<feature>`
  - 개별 작업용 브랜치 이름 규칙.
  - `develop` 이라는 별도 통합 브랜치는 두지 않는다.
  - Git ref 구조상 `develop` 브랜치와 `develop/...` 브랜치를 동시에 둘 수 없으므로, `develop/` 는 prefix 용도로만 사용한다.

## 작업 브랜치 규칙

- 새 작업은 항상 `main` 에서 분기한다.
- 작업 브랜치 이름은 `develop/` prefix 를 사용한다.
- 권장 형식:
  - `develop/<feature>`
  - `develop/<directory>-<feature>`
- 예시:
  - `develop/docs-runtime-flow`
  - `develop/renderer-online-runtime`
  - `develop/resource-page-table`

## 머지 흐름

1. `main` 에서 작업 브랜치를 생성한다.
2. 작업 브랜치에서 구현/수정한다.
3. 작업 브랜치에서 `main` 으로 PR 을 보낸다.
4. 리뷰와 검증이 끝나면 `main` 으로 머지한다.

즉 기본 흐름은 다음과 같다.

```text
main
  <- develop/<feature>
```

## 커밋 메시지 규칙

커밋 메시지는 아래 prefix 중 하나로 시작한다.

- `docs:` 문서 작업
- `feat:` 새로운 기능 추가
- `refactor:` 리팩터링
- `fix:` 코드 수정
- `style:` formatting 수정
- `build:` build 파일 수정

## 커밋 단위 규칙

- 서로 다른 성격의 변경은 한 커밋에 섞지 않는다.
- 문서 수정과 코드 수정이 함께 있으면 분리 커밋한다.
- build 파일 수정이 코드 수정과 함께 있더라도 가능하면 별도 커밋으로 분리한다.
- prefix 하나당 하나의 의미 있는 변경 단위를 만든다.

예시:

```text
docs: document online runtime flow
feat: add target block selection path
fix: correct GPU field cache lookup
build: update renderer target wiring
```

## 디렉터리 단위 작업 권장

merge 충돌을 빠르게 파악하기 위해 가능하면 하나의 PR/merge 에는 하나의 디렉터리 범위만 포함한다.

권장 예시:

- `src/projects/extended_gaussian/renderer/resource/*` 만 수정
- `src/projects/extended_gaussian/renderer/subsystem/rendering_system/*` 만 수정
- `docs/*` 만 수정

피해야 하는 예시:

- `docs/` 와 `src/projects/extended_gaussian/renderer/` 를 한 PR 에 함께 수정
- `renderer/resource/` 와 `renderer/subsystem/rendering_system/` 을 특별한 이유 없이 한 PR 에 함께 수정

단, 기능 특성상 여러 디렉터리를 동시에 만져야 한다면 PR 설명에 이유를 명확히 남긴다.

## PR 기준

- 기본 base branch 는 `main`
- PR 범위는 가능한 작고 명확하게 유지한다.
