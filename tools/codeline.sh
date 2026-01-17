find .. \
  \( -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.html" -o -name "*.js" \) \
  -not -name "httplib.h" \
  -not -path "../client/third_party/*" \
  -not -path "../client/build/*" \
  -not -path "../server/build/*" \
  -exec wc -l {} +