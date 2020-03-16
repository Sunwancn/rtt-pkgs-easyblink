Import('RTT_ROOT')
Import('rtconfig')
from building import *

cwd     = GetCurrentDir()
src     = Glob('easyblink.c') 

CPPPATH = [cwd]

group = DefineGroup('Packages', src, depend = ['PKG_USING_EASYBLINK'], CPPPATH = CPPPATH)

Return('group')