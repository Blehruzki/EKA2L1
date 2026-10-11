# What build.py needs to know about this program.
NAME = 'hello'                 # hello.exe on the phone
UID3 = 0xE0001F01              # 0xE0000000-0xEFFFFFFF: unprotected, no signing needed
CAPTION = 'Hello'              # the name in the phone's menu

# Every import stub in the sources, by DLL. The DLL is named as an SDK build
# names it: module version {000a0000} and its UID3.
IMPORTS = [
    ('euser{000a0000}[100039e5].dll',
     ['userheap_setupthreadheap', 'user_initprocess', 'user_exit',
      'user_panic', 'rdebug_rawprint']),
]
