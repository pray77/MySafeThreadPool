template <typename T>
class SafeQueue
{
public:
	SafeQueue();
	SafeQueue(SafeQueue &&other) {}
	~SafeQueue();

	bool empty() // 返回队列是否为空
	{
		std::unique_lock<std::mutex> lock(m_mutex); //互斥信号变量加锁，防止m_queue被改变
		return m_queue.empty();
	}

	int size()
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		return m_queue.size();
	}

	void enqueue(T &t)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		m_queue.emplace(t);
	}

	bool dequeue(T &t)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_queue.empty())
			return false;
		t = std::move(m_queue.front()); // 右值引用，返回队首元素值

		m_queue.pop();
		return true;
	}

private:
	std::queue<T> m_queue; // 利用模板函数构造队列

	std::mutex m_mutex; // 访问互斥信号量
};
