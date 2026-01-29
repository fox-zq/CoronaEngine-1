# test.py
try:
    import Imgui 
except Exception as e:
    print(e)
import time

def test(func_name):
    if func_name == "add":
        result = add(2, 3)
        return result
    elif func_name == "greet":
        result = greet("World")
        return result
    elif func_name == "open":
        Imgui.create_browser_tab("https://www.baidu.com")
        return True
    else:
        return "Function not found."

# 可以添加更多功能用于测试
def add(a, b):
    return a + b

def greet(name):
    return f"Hello, {name}!"

def test2():
    print("test main")
    if Imgui.create_browser_tab:
        print("start baidu")
        tab = Imgui.create_browser_tab("https://www.baidu.com")
        print("start file")
        tab2 = Imgui.create_browser_tab("file:///E:/workspace/CoronaEngine/build/examples/engine/RelWithDebInfo/test.html")
    else:
        print("no create_browser_tab")