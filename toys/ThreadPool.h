class ThreadPool
{
private:
	class ThreadWorker // 内置线程工作类
	{
	private:
		int m_id; // 工作id

		ThreadPool *m_pool; // 所属线程池
	public:
		// 构造函数
		ThreadWorker(ThreadPool *pool, const int id) : m_pool(pool), m_id(id)
		{
		}

		// 重载()操作
		void operator()()
		{
			std::function<void()> func; // 定义基础函数类func

			bool dequeued; // 是否正在取出队列中元素

			while (!m_pool->m_shutdown)
			{
				{
					// 为线程环境加锁，互访问工作线程的休眠和唤醒
					std::unique_lock<std::mutex> lock(m_pool->m_conditional_mutex);

					// 如果任务队列为空，阻塞当前线程
					if (m_pool->m_queue.empty())
					{
						m_pool->m_conditional_lock.wait(lock); // 等待条件变量通知，开启线程
					}

					// 取出任务队列中的元素
					dequeued = m_pool->m_queue.dequeue(func);
				}

				// 如果成功取出，执行工作函数
				if (dequeued)
					func();
			}
		}
	};

	bool m_shutdown; //线程池是否关闭

	SafeQueue<std::function<void()>> m_queue; // 执行函数安全队列，即任务队列

	std::vector<std::thread> m_threads; // 工作线程队列

	std::mutex m_conditional_mutex; // 线程休眠锁互斥变量

	std::condition_variable m_conditional_lock; // 线程环境锁，可以让线程处于休眠或者唤醒状态

public:
	//线程池构造函数
	ThreadPool(const int n_threads = 4)
		: m_threads(std::vector<std::thread>(n_threads)), m_shutdown(false)
	{

	}

	ThreadPool(const ThreadPool &) = delete; //禁止拷贝构造

	ThreadPool(ThreadPool &&) = delete; // 禁止移动构造

	ThreadPool &operator=(const ThreadPool &) = delete; //禁止复制赋值操作符

	ThreadPool &operator=(ThreadPool &&) = delete; //删除线程池的移动赋值运算符

	// 声明并分配工作线程，将工作线程放入工作线程队列中
	void init()
	{
		for (int i = 0; i < m_threads.size(); ++i)
		{
			m_threads.at(i) = std::thread(ThreadWorker(this, i));//分配工作线程
		}
	}

	// 唤醒所有工作线程，并等待期完成所有工作后关闭线程池
	void shutdown()
	{
		m_shutdown = true;
		m_conditional_lock.notify_all(); //通知，唤醒所有工作线程

		for (int i = 0; i < m_threads.size(); ++i)
		{
			if (m_threads.at(i).joinable()) // 判断线程是否在等待
			{
				m_threads.at(i).join(); // 将线程加入到等待队列
			}
		}
	}
	template<typename F, typename... Args> // typename...为c++11引入的可变模板参数，表示0到任意个数、任意类型的参数，typename F表示至少要有一个模板参数
	// 尾返回类型推导，为了解决返回类型不确定的问题，decltype获取类型，auto关键字将返回类型后置，效果就是该函数的返回值将从std::future<decltype(f(args...))>自动推导得出
	//std::future提供了一个访问异步操作结果的途径。wait()方法设置屏障阻塞线程，get()方法获得执行结果
	//总的来说，submit函数的返回类型为 实例化为f(args...)的std::future<>
	//此处的 && 不是右值引用的意思，是万能引用（当T是模板参数时，T&&的作用主要是保持值类别进行转发）
	auto submit(F &&f, Args &&...args) -> std::future<decltype(f(args...))>
	{
		//std::function可以对多个相似函数进行包装，可以hold住（普通函数，成员函数，lambda，std:bind）
		//std::bind 可以将调用函数的部分参数先预定好，留下一部分在真正调用时确定（也可以直接指定所有参数
		//std::forward()称作完美转发，将会完整保留参数的引用类型进行转发，因为一个绑定到万能引用上的对象可能是左值性也可能是右值性，为了解决这种二义性，产生了std::forward
		//总的来说，这里会产生一个以 函数f(args...) 返回类型 为返回类型、不含参数的特殊函数包装 func()
		std::function<decltype(f(args...))()> func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);

		//make_shared声明了一个shared_ptr智能指针，将func传入作为packaged_task的实例化参数
		//packaged_task可以用来封装任何可以调用的目标，用于实现异步的调用
		auto task_ptr = std::make_shared<std::packaged_task<decltype(f(args...))()>>(func);

		//用function将 task_ptr 指向的packaged_task对象取出并包装为void函数
		std::function<void()) warpper_func = [task_ptr]()
		{
			(*task_ptr)();
		};

		//压入安全队列
		m_queue.enqueue(warpper_func);

		//唤醒一个等待中的线程，条件变量（std::condition_variable）会通知一个处于wait状态的线程，该线程将会从任务队列中取得任务并执行。
		//条件变量是为了解决死锁而生，当互斥操作不够用而引用的。
		//比如当线程需要等待某个条件为真才能继续执行，而一个忙等待循环中可能会导致所有其他线程都无法进入临界区使得条件为真时，就会发生死锁
		//忙等待是指在单CPU情况下，一个进程进入临界区之后，其他进程因无法满足竞争条件而循环探测竞争条件，缺点是会浪费时间片
		//condition_variable实例被创建出来主要就是用于唤醒等待线程从而避免死锁。
		//norify_one用于唤醒一个线程；notify_all则是通知所有线程
		m_conditional_lock.notify_one();

		//返回先前注册的任务指针
		return task_ptr->get_future();

	}

};
