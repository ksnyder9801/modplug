/*
 * TestToolsLib.h
 * --------------
 * Purpose: Unit test framework for libopenmpt.
 * Notes  : Currently somewhat unreadable :/
 * Authors: OpenMPT Devs
 * The OpenMPT source code is released under the BSD license. Read LICENSE for more details.
 */


#pragma once


#ifdef ENABLE_TESTS
#ifndef MODPLUG_TRACKER


namespace MptTest
{


extern int fail_count;


enum Verbosity
{
	VerbosityQuiet,
	VerbosityNormal,
	VerbosityVerbose,
};

enum Fatality
{
	FatalityContinue,
	FatalityStop
};


struct Context
{
public:
	const char * const file;
	const int line;
public:
	Context(const char * file, int line);
	Context(const Context &c);
};

#define MPT_TEST_CONTEXT_CURRENT() (::MptTest::Context( __FILE__ , __LINE__ ))


struct TestFailed
{
	std::string values;
	TestFailed(const std::string &values) : values(values) { }
	TestFailed() { }
};


class Test
{

private:

	Fatality const fatality;
	Verbosity const verbosity;
	const char * const desc;
	Context const context;

public:

	Test(Fatality fatality, Verbosity verbosity, const char * const desc, const Context &context);

public:

	std::string AsString() const;

	void ShowStart() const;
	void ShowProgress(const char * text) const;
	void ShowPass() const;
	void ShowFail(bool exception = false, const char * const text = nullptr) const;

	void ReportPassed();
	void ReportFailed();

	void ReportException();

public:

#if 0 // C++11 version

private:

	template <typename Tx, typename Ty>
	noinline void TypeCompareHelper(const Tx &x, const Ty &y)
	{
		if(x != y)
		{
			//throw TestFailed(mpt::String::Print("%1 != %2", x, y));
			throw TestFailed();
		}
	}

public:

	template <typename Tfx, typename Tfy>
	noinline void operator () (const Tfx &fx, const Tfy &fy)
	{
		ShowStart();
		try
		{
			ShowProgress("Calculate x ...");
			const auto x = fx();
			ShowProgress("Calculate y ...");
			const auto y = fy();
			ShowProgress("Compare ...");
			TypeCompareHelper(x, y);
			ReportPassed();
		} catch(...)
		{
			ReportFailed();
		}
	}

	#define VERIFY_EQUAL(x,y)	::MptTest::Test(::MptTest::FatalityContinue, ::MptTest::VerbosityNormal, #x " == " #y , MPT_TEST_CONTEXT_CURRENT() )( [&](){return (x) ;}, [&](){return (y) ;} )
	#define VERIFY_EQUAL_NONCONT(x,y)	::MptTest::Test(::MptTest::FatalityStop, ::MptTest::VerbosityNormal, #x " == " #y , MPT_TEST_CONTEXT_CURRENT() )( [&](){return (x) ;}, [&](){return (y) ;} )
	#define VERIFY_EQUAL_QUIET_NONCONT(x,y)	::MptTest::Test(::MptTest::FatalityStop, ::MptTest::VerbosityQuiet, #x " == " #y , MPT_TEST_CONTEXT_CURRENT() )( [&](){return (x) ;}, [&](){return (y) ;} )

#else

public:

	template <typename Tx, typename Ty>
	noinline void operator () (const Tx &x, const Ty &y)
	{
		ShowStart();
		try
		{
			if(x != y)
			{
				//throw TestFailed(mpt::String::Print("%1 != %2", x, y));
				throw TestFailed();
			}
			ReportPassed();
		} catch(...)
		{
			ReportFailed();
		}
	}

	#define VERIFY_EQUAL(x,y)	::MptTest::Test(::MptTest::FatalityContinue, ::MptTest::VerbosityNormal, #x " == " #y , MPT_TEST_CONTEXT_CURRENT() )( (x) , (y) )
	#define VERIFY_EQUAL_NONCONT(x,y)	::MptTest::Test(::MptTest::FatalityStop, ::MptTest::VerbosityNormal, #x " == " #y , MPT_TEST_CONTEXT_CURRENT() )( (x) , (y) )
	#define VERIFY_EQUAL_QUIET_NONCONT(x,y)	::MptTest::Test(::MptTest::FatalityStop, ::MptTest::VerbosityQuiet, #x " == " #y , MPT_TEST_CONTEXT_CURRENT() )( (x) , (y) )

#endif

};


#define DO_TEST(func) \
do{ \
	::MptTest::Test test(::MptTest::FatalityStop, ::MptTest::VerbosityNormal, #func , MPT_TEST_CONTEXT_CURRENT() ); \
	try { \
		test.ShowStart(); \
		fail_count = 0; \
		func(); \
		if(fail_count > 0) { \
			throw ::MptTest::TestFailed(); \
		} \
		test.ReportPassed(); \
	} catch(...) { \
		test.ReportException(); \
	} \
}while(0)


} // namespace MptTest


#endif // !MODPLUG_TRACKER
#endif // ENABLE_TESTS
