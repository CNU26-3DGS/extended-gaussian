# Extended Gaussian 빌드 가이드

이 문서는 Windows 환경에서 기존 개발자용 뷰어와 유저용 뷰어를 빌드하고 실행하는 방법을 정리한다.

## 전제 조건

- Visual Studio 2019 이상 또는 호환되는 MSVC C++ 빌드 도구
- CMake 3.16 이상
- CUDA Toolkit
- Git submodule과 외부 라이브러리가 정상적으로 내려받아진 저장소

명령은 저장소 루트에서 실행한다.

```powershell
cd C:\Users\ilhyeonchu\Documents\GitHub\extended-gaussian
```

## CMake Configure

처음 빌드하거나 CMake 설정이 바뀐 경우 configure를 실행한다.

```powershell
cmake -S . -B build
```

기존 `build` 디렉터리를 재사용할 수 없거나 generator 설정이 꼬인 경우에는 별도 빌드 디렉터리를 만들어 configure한다.

```powershell
cmake -S . -B build-vs
```

## 유저용 뷰어 빌드

유저용 실행 파일 타겟은 `extended_gaussianUserViewer_app`이다.

```powershell
cmake --build build --target extended_gaussianUserViewer_app --config RelWithDebInfo
```

빌드 결과는 기본적으로 다음 위치에 생성된다.

```powershell
.\install\bin\extended_gaussianUserViewer_app.exe
```

## 개발자용 뷰어 빌드

기존 개발자용 실행 파일 타겟은 `extended_gaussianViewer_app`이다.

```powershell
cmake --build build --target extended_gaussianViewer_app --config RelWithDebInfo
```

빌드 결과는 기본적으로 다음 위치에 생성된다.

```powershell
.\install\bin\extended_gaussianViewer_app_rwdi.exe
```

## 유저용 뷰어 실행

manifest를 바로 지정하려면 `--manifest` 인자를 사용한다.

```powershell
.\install\bin\extended_gaussianUserViewer_app.exe --manifest .\manifests\example_manifest.json
```

manifest나 PLY를 실행 후 선택하려면 인자 없이 실행한다.

```powershell
.\install\bin\extended_gaussianUserViewer_app.exe
```

유저용 뷰어는 시작 시 데이터 선택 창을 표시하며, 여기서 manifest JSON 또는 단일 PLY 파일을 선택할 수 있다.

## 실행 후 확인 항목

- 좌상단 미니맵이 창 크기에 따라 줄거나 커지는지 확인한다.
- 미니맵의 `고정` / `입력` 버튼으로 401~416 고정 지도와 입력 데이터 기반 지도가 전환되는지 확인한다.
- `Full` / `Focus` 버튼으로 전체 지도와 선택 블록 중심 표시가 전환되는지 확인한다.
- 오른쪽 즐겨찾기 목록과 좌하단 WASD 패드가 창 크기 변경에 맞춰 함께 스케일되는지 확인한다.
- PLY 또는 manifest 선택 후 블록이나 instance 클릭 시 카메라가 해당 중심으로 이동하는지 확인한다.

## 자주 쓰는 검증 명령

두 실행 파일을 모두 확인하려면 다음 두 명령을 순서대로 실행한다.

```powershell
cmake --build build --target extended_gaussianUserViewer_app --config RelWithDebInfo
cmake --build build --target extended_gaussianViewer_app --config RelWithDebInfo
```

빌드 중 기존 코드의 `strcpy` 관련 MSVC warning이 보일 수 있다. 현재 warning은 빌드를 막지 않는다.
