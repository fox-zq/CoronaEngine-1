import json
import sys
import time

# 检查 Imgui
try:
    import Imgui
    HAS_IMGUI = True
except ImportError:
    HAS_IMGUI = False


tab_list = []

def handle_request(json_str):
    """
    处理 JSON 格式的请求
    格式: {"function": "function_name", "args": [arg1, arg2, ...]}
    """
    try:
        request = json.loads(json_str)
        
        func_name = request.get('function')
        args = request.get('args', [])
        
        if not func_name:
            return create_error_response("Missing 'function' field")
        
        # 获取函数
        if func_name == "greet":
            result = greet(*args) if args else greet()
        elif func_name == "add":
            result = add(*args) if len(args) >= 2 else "add requires two arguments"
        elif func_name == "multiply":
            result = multiply(*args) if len(args) >= 2 else "multiply requires two arguments"
        elif func_name == "get_info":
            result = get_info()
        elif func_name == "calculate":
            result = calculate(*args)
        elif func_name == "open_browser":
            result = open_browser(*args) if args else open_browser()
        elif func_name == "get_version":
            result = get_version()
        elif func_name == "js_call_func":
        
            args_str = ','.join(
                f"'{arg}'" if isinstance(arg, str) else str(arg)
                for arg in args
            )

            js_code = f"""
                window.test("{func_name}({args_str})");
            """
            for i in tab_list:
                Imgui.execute_javascript(i,js_code)
            result = f"Called js function '{func_name}' with args: {args_str}"
        else:
            # 尝试动态调用函数
            if hasattr(sys.modules[__name__], func_name):
                func = getattr(sys.modules[__name__], func_name)
                if callable(func):
                    result = func(*args) if args else func()
                else:
                    return create_error_response(f"'{func_name}' is not callable")
            else:
                return create_error_response(f"Function '{func_name}' not found")
        
        return create_success_response(result)
        
    except json.JSONDecodeError as e:
        return create_error_response(f"Invalid JSON: {str(e)}")
    except Exception as e:
        return create_error_response(f"Error processing request: {str(e)}")

# 工具函数：创建标准响应
def create_success_response(data):
    """创建成功响应"""
    return json.dumps({
        "success": True,
        "data": data,
        "timestamp": time.time()
    })

def create_error_response(message):
    """创建错误响应"""
    return json.dumps({
        "success": False,
        "error": message,
        "timestamp": time.time()
    })

# 具体功能函数
def greet(name="World"):
    return f"Hello, {name}! From Python"

def add(a, b):
    return a + b

def multiply(a, b):
    return a * b

def calculate(params):
    """处理更复杂的参数"""
    if isinstance(params, dict):
        a = params.get('a', 0)
        b = params.get('b', 0)
        operation = params.get('operation', 'add')
        
        if operation == 'add':
            return a + b
        elif operation == 'multiply':
            return a * b
        elif operation == 'subtract':
            return a - b
        elif operation == 'divide':
            return a / b if b != 0 else "Division by zero"
        else:
            return f"Unknown operation: {operation}"
    else:
        return "Expected dictionary parameters"

def get_info():
    return {
        "module": __name__,
        "python_version": sys.version.split()[0],
        "has_imgui": HAS_IMGUI,
        "functions_available": ["greet", "add", "multiply", "calculate", "get_info", "get_version"]
    }

def get_version():
    return {
        "api_version": "1.0",
        "python_version": sys.version,
        "platform": sys.platform
    }

def open_browser(url="https://www.baidu.com", path=""):
    if HAS_IMGUI:
        try:
            tab_id = Imgui.create_browser_tab(url, path)
            tab_list.append(tab_id)
            return f"Opened: {url}"
        except Exception as e:
            return f"Failed to open browser: {str(e)}"
    else:
        return f"Imgui not available. Would open: {url}"

if __name__ == "__main__":
    # 测试代码
    print("JSON API module loaded")
    
    # 测试 JSON 处理
    test_requests = [
        '{"function": "greet", "args": ["Developer"]}',
        '{"function": "add", "args": [5, 3]}',
        '{"function": "multiply", "args": [4, 7]}',
        '{"function": "calculate", "args": [{"a": 10, "b": 20, "operation": "add"}]}',
        '{"function": "unknown_function", "args": []}'
    ]
    
    for req in test_requests:
        print(f"\nTest: {req}")
        print(f"Result: {handle_request(req)}")