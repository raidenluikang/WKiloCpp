
#include <string_view>
#include <cstddef> // size_t
#include <span>

//#ifndef STDOUT_FILENO
//#define STDOUT_FILENO 1
//#endif
//#ifndef STDIN_FILENO
//#define STDIN_FILENO 0
//#endif
//

namespace wkilocpp
{

	struct ScreenSize
	{
		size_t rows;
		size_t cols;
	};

	struct ScreenHandle
	{
	public:
		ScreenHandle();
		~ScreenHandle();

		ScreenHandle(const ScreenHandle&) = delete;
		ScreenHandle& operator = (const ScreenHandle&) = delete;


		void enableRawMode();

		int winRead( /*int ignored,*/ std::span<char> buf);

		int winWrite( /*int ignored,*/ std::span<const char> cbuf);

		/*guaranteed that screen size row and cols in [1..8192] */
		ScreenSize getWindowSize() const;

		void disableRawMode();
	private:
		struct impl;
		struct impl* d_;
	};
	

	
	//DWORD is unsigned long in Windows System.
	unsigned long winGetLastError();

} // wkilocpp