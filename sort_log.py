from re import search
from os import mkdir
from sys import argv

daemons = ['pacemakerd', 'lrmd', 'attrd', 'cib', 'pengine', 'crmd', 'stonithd']

def sort_log(folder, file):
	log = open(folder + '/' + file)
	write_dict = {}
	for daemon in daemons:
		try:
			mkdir(folder + '/' + daemon)
		except Exception:
			pass
		write_dict[daemon] = open(folder + '/' + daemon + '/' + file, 'w')
	for line in log:
		key = search('sasha\s*(\w*): \(', line)
		if key: write_dict[key.group(1)].write(line)
	for w in write_dict.values():
		w.close()
	log.close()

if len(argv) == 2:
	args = argv[1].split('/')
	sort_log('/'.join(args[:-1]), args[-1])
else:
	print 'Wrong args number'
