#!/usr/bin/env bash
#
# 檢查每一份 spec 的中英文版結構是否對得起來。
#
# 只比對「結構」而不是內容：章節數、Requirement 數、Scenario 數，以及每個
# Requirement 底下的 Scenario 數。標題文字本來就會被翻譯，所以不比字面。
#
# 這支腳本存在的理由：雙語慣例若只寫在 README 上，遲早會有人只改一邊。
set -uo pipefail
cd "$(dirname "$0")/specs"

fail=0

count() { grep -c "^$2" "$1" 2>/dev/null || echo 0; }

for en in */spec.md; do
    domain="$(dirname "$en")"
    zh="$domain/spec.zh-TW.md"

    if [[ ! -f "$zh" ]]; then
        echo "!! $domain: 缺少 spec.zh-TW.md"
        fail=1
        continue
    fi

    ok=1
    for pattern in '## ' '### Requirement:' '#### Scenario:'; do
        a=$(count "$en" "$pattern")
        b=$(count "$zh" "$pattern")
        if [[ "$a" != "$b" ]]; then
            echo "!! $domain: '$pattern' 數量不符 — 英文 $a，中文 $b"
            ok=0
            fail=1
        fi
    done

    # 每個 Requirement 底下的 Scenario 數也要一致（順序敏感）
    seq_of() {
        awk '/^### Requirement:/ {if (n!="") print c; n=$0; c=0; next}
             /^#### Scenario:/ {c++}
             END {if (n!="") print c}' "$1" | tr '\n' ',' 
    }
    sa=$(seq_of "$en"); sb=$(seq_of "$zh")
    if [[ "$sa" != "$sb" ]]; then
        echo "!! $domain: 各 Requirement 的 Scenario 數不符"
        echo "     英文: $sa"
        echo "     中文: $sb"
        ok=0
        fail=1
    fi

    [[ $ok == 1 ]] && printf 'ok  %-22s %s Requirement / %s Scenario\n' \
        "$domain" "$(count "$en" '### Requirement:')" "$(count "$en" '#### Scenario:')"
done

echo
if [[ $fail == 0 ]]; then
    echo "全部 spec 的中英文結構一致"
else
    echo "有 spec 的中英文結構不一致 —— 兩份要在同一個 commit 內同步更新"
fi
exit $fail
