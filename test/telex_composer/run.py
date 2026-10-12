import pathlib, subprocess, sys, unicodedata, json
root = pathlib.Path(__file__).resolve().parents[2]
pairs = []
# The Unicode standard is an independent oracle for all 144 vowel and tone pairs.
shapes = [('a','a'),('aw','ă'),('aa','â'),('e','e'),('ee','ê'),('i','i'),('o','o'),('oo','ô'),('ow','ơ'),('u','u'),('uw','ư'),('y','y')]
tones = [('', ''), ('s','\u0301'), ('f','\u0300'), ('r','\u0309'), ('x','\u0303'), ('j','\u0323')]
for keys, base in shapes:
    for tone, combining in tones:
        value = unicodedata.normalize('NFC', base + combining)
        pairs.extend([(keys + tone, value), ((keys + tone).upper(), value.upper())])
words = '''
nguowif người
nguwowif người
nguwowfi người
quyeern quyển
quyeenr quyển
giuwax giữa
giuwxa giữa
khuya khuya
thuowr thuở
thuongw thương
thuowng thương
thuowngr thưởng
huow huơ
huowng hương
duowngf dường
dduowngf đường
thieen thiên
thieens thiến
thieenj thiện
nguyeenx nguyễn
hoaf hoà
hoas hoá
hoar hoả
hoef hoè
thuyr thuỷ
thuyj thuỵ
quas quá
quar quả
quyf quỳ
quis quí
gias giá
giaf già
gif gì
quans quán
quaatj quật
giaso giáo
giaos giáo
gieengs giếng
giuowis giưới
nguowcs ngước
truowngf trường
chuowng chương
chuoong chuông
buoomf buồm
bieenr biển
mias mía
muas múa
muaf mùa
cuar của
buooir buổi
boois bối
ngoaif ngoài
ngoaij ngoại
ngoaifs ngoái
nguyeen nguyên
yeeus yếu
yeeu yêu
khoer khoẻ
khoanr khoản
ddang đang
daan dân
ddawng đăng
ddawngs đắng
ddatj đạt
ddepj đẹp
ddiaj địa
vieetj việt
haf hà
nooij nội
hanhj hạnh
phucs phúc
vieecj việc
tieengs tiếng
tieenf tiền
thuoocs thuốc
thuoojc thuộc
tuooir tuổi
muuw mưu
cuuws cứu
ruowuj rượu
ruwowuj rượu
ngoawnf ngoằn
hoawcj hoặc
nghieeng nghiêng
ass as
aff af
arr ar
axx ax
ajj aj
asf à
asz a
aasz â
z z
ss ss
fff fff
aszz az
aws ắ
aaa aa
eee ee
ooo oo
ddd dd
aww aw
oww ow
uww uw
w ư
ww w
tw tư
ngw ngư
tws tứ
tww tw
WWW WW
Ddas Đá
Ddaas Đấ
nguwowif< ngườ
'''
for line in words.strip().splitlines():
    raw, expected = line.split(' ', 1)
    pairs.append((raw, expected))
    if '<' not in raw:
        pairs.append((raw.upper(), expected.upper()))
        pairs.append((raw[0].upper() + raw[1:], expected[0].upper() + expected[1:]))
pairs.extend([('Thieen DDuowngf', 'Thiên Đường'), ('Nguyeenx Vawn An', 'Nguyễn Văn An'),
              ('aa,as;ee!uw?', 'â,á;ê!ư?'), ('khuya khuys', 'khuya khuý')])
result = subprocess.run([sys.argv[1]], input='\n'.join(k for k,v in pairs)+'\n', text=True, capture_output=True)
actual = result.stdout.splitlines()
failed=[]
for (raw,expected), value in zip(pairs, actual):
    if value != expected or unicodedata.normalize('NFC',value) != value:
        failed.append((raw,expected,value))
for raw,expected,value in failed:
    print(f'{raw!r}: expected {expected!r}, got {value!r}')
print(f'cases={len(pairs)} failed={len(failed)} exit={result.returncode} outputs={len(actual)}')
if result.stderr: print(result.stderr)
if failed or result.returncode or len(actual)!=len(pairs): sys.exit(1)
