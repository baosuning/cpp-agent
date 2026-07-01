import sys

# Windows: 强制控制台使用 UTF-8 代码页，避免输出被终端按 GBK 解析为乱码
if sys.platform == "win32":
    try:
        import ctypes
        ctypes.windll.kernel32.SetConsoleOutputCP(65001)
        ctypes.windll.kernel32.SetConsoleCP(65001)
    except Exception:
        pass

from pypdf import PdfReader




reader = PdfReader(sys.argv[1])
if (reader.get_fields()):
    print("This PDF has fillable form fields")
else:
    print("This PDF does not have fillable form fields; you will need to visually determine where to enter data")
