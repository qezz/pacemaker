import re
from sys import argv
from os import walk
import gc

trace = '\n\tcrm_info("trace");'

'''def add_traces(path):
	file = open(path)
	code = file.read()
	reg = re.compile('(\w+\s+\*?\s*(\w+)\s*\((((const\s)?(enum\s)?\w+\s+(const\s?)?\*?\s*\w+(\,\s+)?)*)\)\s*\{)')
	for k in reg.finditer(code):
		print k.span(), code[slice(*k.span())]
	file.close()
'''

def recursive_remove_traces(path = '.'):
	for (dirpath, dirnames, filenames) in walk(path):
		for directory in dirnames:
			if directory[0] != '.' and directory not in ['ais', 'gnu']: recursive_remove_traces(path + '/' + directory)
		for file in filenames:
			if file[-2:] == '.c':
				print path + '/' + file
				remove_traces(path + '/' + file)
		break

def remove_traces(path):
	file = open(path)
	text = file.read().replace(trace, '')
	output = open(path, 'w')
	output.write(text)
	file.close()
	output.close()

def recursive_add_traces(path = '.'):
	for (dirpath, dirnames, filenames) in walk(path):
		for directory in dirnames:
			if directory[0] != '.' and directory not in ['ais', 'gnu']: recursive_add_traces(path + '/' + directory)
		for file in filenames:
			if file[-2:] == '.c':
				print path + '/' + file
				add_traces(path + '/' + file)
		break

def recursive_remove_declaration_after_statement(path = '.'):
	for (dirpath, dirnames, filenames) in walk(path):
		for directory in dirnames:
			if directory[0] != '.': recursive_remove_declaration_after_statement(path + '/' + directory)
		for file in filenames:
			if file == 'Makefile':
				print path + '/' + file
				remove_declaration_after_statement(path + '/' + file)
		break

def remove_declaration_after_statement(path):
	file = open(path)
	text = file.read().replace('-Wdeclaration-after-statement', '')
	output = open(path, 'w')
	output.write(text)
	file.close()
	output.close()


def add_traces(path, replace = True, display = False, trace_to_info = True):
	file = open(path)
	code = file.read()
	if trace_to_info: code.replace('crm_trace(', 'crm_info(')
	output = open(path, 'w') if replace else open(path[:-2] + '_mod.' + path[-1]	, 'w')

	#if 'include <crm/common/logging.h>' not in code: output.write('#include <crm/common/logging.h>\n')

	#egex = re.compile('(\w+\s+\*?\s*\w+\s*\((((const\s)?(enum\s)?\w+\s+(const\s?)?\*?\s*\w+(\,\s+)?)*|void)\)\s*\{)(?!%s)' % trace.replace(')', '\)').replace('(','\('))
	regex = re.compile(r'(?<!#)(?:\b\w+\s+\*?\s*\w+\s*\((((const\s)?(enum\s|struct\s)?\w+\s+(const\s?)?\**\s*\w+(\,\s+)?)*?|void)\)\s*\{)(?!%s)' % trace.replace(')', '\)').replace('(','\('))
	while 1:
		m = regex.search(code)
		if m:
			if display: print m.group()
			position = m.end()
			output.write(code[:position] + trace)
			code = code[position:]
			gc.collect()
		else:
			output.write(code)
			break
	del code
	gc.collect()
	file.close()
	output.close()

'''

if len(argv) == 2:
	add_traces(argv[1], False, True)
else:
	print 'Wrong args number'
'''


''''''
#add_recirsive_traces()
#recursive_remove_declaration_after_statement()



recursive_add_traces('./pengine')
recursive_add_traces('./cib')
recursive_add_traces('./crmd')
recursive_add_traces('./lrmd')
recursive_add_traces('./lib')

remove_traces('./lib/common/mainloop.c')
remove_traces('./lib/common/xml.c')
remove_traces('./lib/common/logging.c')
remove_traces('./lib/common/utils.c')

#recursive_remove_traces()


'''add_traces('./lib/common/mainloop.c')'''

#add_traces('./lib/pengine/unpack.c')

'''add_traces('./lib/services/services_linux.c', False, True)'''
