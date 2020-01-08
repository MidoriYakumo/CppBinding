
struct Binding
{
	virtual ~Binding() {}

	virtual void update() {}
};

enum class BindingCompPolicy
{
	NotEqual = 0,
	Equal    = 1,
	Always   = 2,
	Count
};

enum class BindingEvalPolicy
{
	Instant = 0,
	Lazy    = 1,
	Count
};

#include <list>
#include <memory>

template <typename T>
struct TypedBindingPtr;

template <typename T>
struct TypedBinding : Binding
{
	using ValueType = T;

	T                   m_value{};
	BindingCompPolicy   m_comp_policy{BindingCompPolicy::NotEqual};
	std::list<Binding*> m_refs; // weak refs, erase type

	virtual T get()
	{
		return m_value;
	}

	virtual void set(T value)
	{
		if (different_with(value))
		{
			m_value = value;
			for (auto ref : m_refs)
			{
				ref->update();
			}
		}
	}

	virtual bool different_with(T other)
	{
		switch (m_comp_policy)
		{
		case BindingCompPolicy::NotEqual:
		{
			return m_value != other;
		}
		case BindingCompPolicy::Equal:
		{
			return !(m_value == other);
		}
		case BindingCompPolicy::Always:
		case BindingCompPolicy::Count:
		default:
		{
			return true;
		}
		}
	}
};

template <typename T>
struct TypedBindingPtr : std::shared_ptr<TypedBinding<T>>
{
	operator T()
	{
		return this->get()->get();
	}
};

template <typename T>
struct TypedBindedValue : TypedBinding<T>
{
	TypedBindedValue(T value)
	{
		this->set(value);
	}
};

template <typename T>
struct TypedBindedValuePtr : TypedBindingPtr<T>
{
	TypedBindedValuePtr(T value)
	{
		this->reset(new TypedBindedValue<T>(value));
	}

	TypedBindedValuePtr& operator=(T value)
	{
		this->get()->set(value);
		return *this;
	}
};

#include <functional>

template <bool P, typename T = void>
using enable_if_t = typename std::enable_if<P, T>::type;

template <typename Ret, typename... Params>
struct TypedBindedExpr : TypedBinding<Ret>
{
	using FuncType = std::function<Ret(Params...)>;

	BindingEvalPolicy                      m_eval_policy{BindingEvalPolicy::Instant};
	FuncType                               m_function;
	std::tuple<TypedBindingPtr<Params>...> m_arguments;
	bool                                   m_dirty{};

	TypedBindedExpr(FuncType function, TypedBindingPtr<Params>... arguments)
		: m_function(function)
		, m_arguments(std::make_tuple(arguments...))
	{
		m_dirty = m_eval_policy == BindingEvalPolicy::Instant;
		process_refs<true, 0>();
		update();
	}

	~TypedBindedExpr() override
	{
		process_refs<false, 0>();
	}

	Ret get() override
	{
		if (m_dirty)
		{
			this->set(dispatch());
			m_dirty = false;
		}
		return TypedBinding<Ret>::get();
	}

	void update() override
	{
		switch (m_eval_policy)
		{
		case BindingEvalPolicy::Instant:
		{
			this->set(dispatch());
			break;
		}
		case BindingEvalPolicy::Lazy:
		{
			m_dirty = true;
			break;
		}
		case BindingEvalPolicy::Count:
		default:
		{}
		}
	}

	template <typename... Args>
	enable_if_t<sizeof...(Args) != sizeof...(Params), Ret> dispatch(Args&&... args) const
	{
		return dispatch(
				std::forward<Args>(args)..., std::get<sizeof...(Args)>(m_arguments)->get());
	}

	template <typename... Args>
	enable_if_t<sizeof...(Args) != sizeof...(Params), Ret> dispatch(Args&&... args)
	{
		return dispatch(
				std::forward<Args>(args)..., std::get<sizeof...(Args)>(m_arguments)->get());
	}

	Ret dispatch(Params... args) const
	{
		return m_function(std::forward<Params>(args)...);
	}

	Ret dispatch(Params... args)
	{
		return m_function(std::forward<Params>(args)...);
	}

	template <typename T>
	void install_ref(T arg)
	{
		arg->m_refs.push_back(this);
	}

	template <typename T>
	void uninstall_ref(TypedBindingPtr<T> arg)
	{
		arg->m_refs.remove_if([this](Binding* ref) { return ref == this; });
	}

	template <bool Install, size_t N>
	enable_if_t<N != sizeof...(Params)> process_refs()
	{
		Install ? install_ref(std::get<N>(m_arguments))
				: uninstall_ref(std::get<N>(m_arguments));
		process_refs<Install, N + 1>();
	}

	template <bool Install, size_t N>
	enable_if_t<N == sizeof...(Params)> process_refs()
	{}
};

template <typename Ret, typename... Params>
struct TypedBindedExprPtr : TypedBindingPtr<Ret>
{
	template <typename... Args>
	TypedBindedExprPtr(Args... args)
	{
		this->reset(new TypedBindedExpr<Ret, Params...>(args...));
	}
};

template <class Op, typename TL, typename TR>
struct BinaryOpTraits
{
	using RetType = decltype(std::declval<Op>()(std::declval<TL>(), std::declval<TR>()));
};

template <class Op, typename TL, typename TR>
using BinaryOpRetType = typename BinaryOpTraits<Op, TL, TR>::RetType;

template <typename TL, typename TR>
TypedBindedExprPtr<BinaryOpRetType<std::plus<>, TL, TR>, TL, TR>
operator+(TypedBindingPtr<TL> lhs, TypedBindingPtr<TR> rhs)
{
	return {std::plus<>{}, lhs, rhs};
}

#include <cmath>
#include <iostream>

int main()
{
	{
		TypedBindedValuePtr<int> a = 1, b = 2;
		auto                     e = a + b;
		std::cout << int(a) << " + " << int(b) << " = " << int(e) << std::endl;
		a = 3;
		std::cout << int(a) << " + " << int(b) << " = " << int(e) << std::endl;
	}
	{
		TypedBindedValuePtr<int> a = 1, b = 2;
		auto                     e  = a + b;
		auto                     pe = dynamic_cast<TypedBindedExpr<int, int, int>*>(e.get());
		pe->m_eval_policy           = BindingEvalPolicy::Lazy;
		std::cout << int(a) << " + " << int(b) << " = " << int(e) << std::endl;
		a = 3;
		std::cout << "value: " << e->m_value << ", dirty: " << pe->m_dirty << std::endl;
		std::cout << int(a) << " + " << int(b) << " = " << int(e) << std::endl;
	}
	{
		TypedBindedValuePtr<int> a = 1, b = 2, c = 3;
		auto                     e = (a + b) + (a + c);
		std::cout << int(e) << std::endl;
		a = 5; // a = d; // bad definition
		std::cout << int(e) << std::endl;
	}
	{
		TypedBindedValuePtr<int>       a = 1;
		TypedBindedExprPtr<float, int> e(sin, a);
		std::cout << float(e) << std::endl;
		a = 3;
		std::cout << float(e) << std::endl;
	}
	return 0;
}
