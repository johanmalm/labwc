#!/bin/sh

banned="malloc g_strcmp0"

main () {
	find src/ include/ \( -name "*.c" -o -name "*.h" \) -type f |
	{
		errors=0
		while IFS= read -r file; do
			./scripts/helper/find-idents "$file" $banned || {
				printf '%b\n' "$file"
				errors=1
			}
		done
		return ${errors}
	}
}

main
