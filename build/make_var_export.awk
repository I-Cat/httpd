# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# Based on apr's make_export.awk, which is
# based on Ryan Bloom's make_export.pl

/^#[ \t]*if(def)? (AP[RU]?_|!?defined).*/ {
	if (old_filename != FILENAME) {
		if (old_filename != "") printf("%s", line)
		macro_no = 0
		found = 0
		count = 0
		old_filename = FILENAME
		line = ""
	}
	macro_stack[macro_no++] = macro
	macro = substr($0, length($1)+2)
	count++
	line = line "#ifdef " macro "\n"
	next
}

/^#[ \t]*endif/ {
	if (count > 0) {
		count--
		line = line "#endif /* " macro " */\n"
		macro = macro_stack[--macro_no]
	}
	if (count == 0) {
		if (found != 0) {
			printf("%s", line)
		}
		line = ""
	}
	next
}

function add_symbol (sym_name) {
	if (count) {
		found++
	}
	for (i = 0; i < count; i++) {
		line = line "\t"
	}
	line = line sym_name "\n"

	if (count == 0) {
		printf("%s", line)
		line = ""
	}
}

/^[ \t]*(extern[ \t]+)?AP[RU]?_DECLARE_DATA .*;$/ {
       varname = $NF;
       gsub( /[*;]/, "", varname);
       gsub( /\[.*\]/, "", varname);
       add_symbol(varname);
}

END {
	printf("%s", line)
}
