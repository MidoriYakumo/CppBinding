# CppBinding
A demo implementing data binding in C++

```c++
	TypedBindedValuePtr<int> a = 1, b = 2;
	auto e = a + b; // e is an expression with a + b
	std::cout << int(e); // it's 3
	a = 3;
	std::cout << int(e); // it's 5 now
```
