## CPPCHECK使用

cppcheck --library=qt --platform=win64 --std=c++11 --xml-version=2 --enable=warning,style,performance,portability --force --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction -I . -I src -i build -i bin -i lib --output-file=cppcheck-report-win.xml main.cpp src



cppcheck --library=qt --platform=unix64 --std=c++11 --xml-version=2 --enable=warning,style,performance,portability --force --inline-suppr --suppress=missingIncludeSystem --suppress=unusedFunction -U_WIN32 -UWIN32 -U_WIN64 -U_MSC_VER -D__linux__ -D__GNUC__=11 -I . -I src -i build -i bin -i lib --output-file=cppcheck-report-linux.xml main.cpp src