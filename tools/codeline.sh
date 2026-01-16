find .. \
  \( -name "*.cpp" -o -name "*.h" -o -name "*.py" -o -name "*.html" -o -name "*.js" \) \
  -not -name "httplib.h" \
  -not -path "../client/src/jsondist/*" \
  -exec wc -l {} +