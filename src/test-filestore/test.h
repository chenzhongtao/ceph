#ifndef TEST_H
#define TEST_H


#define TEST(x, y) void y()

#define ASSERT_EQ(v, s) if(v != s)cout << "Error at " << __LINE__ << "(" << #v << "!= " << #s << "\n"; \
                                else cout << "(" << #v << "==" << #s << ") PASSED\n";                               
#define EXPECT_EQ(v, s) ASSERT_EQ(v, s)

#define ASSERT_NE(v, s) if(v == s)cout << "Error at " << __LINE__ << "(" << #v << "== " << #s << "\n"; \
                                else cout << "(" << #v << "!=" << #s << ") PASSED\n";
#define ASSERT_GE(v, s) if(v < s)cout << "Error at " << __LINE__ << "(" << #v << "== " << #s << "\n"; \
                                else cout << "(" << #v << "!=" << #s << ") PASSED\n";
#define ASSERT_GT(v, s) if(v <= s)cout << "Error at " << __LINE__ << "(" << #v << "== " << #s << "\n"; \
                                else cout << "(" << #v << "!=" << #s << ") PASSED\n";
#define ASSERT_LT(v, s) if(v >= s)cout << "Error at " << __LINE__ << "(" << #v << "== " << #s << "\n"; \
                                else cout << "(" << #v << "!=" << #s << ") PASSED\n";
#define EXPECT_LT(v, s)  ASSERT_LT(v, s)  

#define ASSERT_TRUE(c) if(!(c))cout << "Error at " << __LINE__ << "(" << #c << ")" << "\n"; \
                          else cout << "(" << #c << ") PASSED\n";
#define EXPECT_TRUE(c) ASSERT_TRUE(c)

#define ASSERT_FALSE(c) if(c)cout << "Error at " << __LINE__ << "(" << #c << ")" << "\n"; \
                          else cout << "(" << #c << ") PASSED\n";

bool if_str_in_argv(int argc, char **argv, string str)
{
    int i = 0;
    for (i=0; i<argc; i++) {
        if (!strcmp(argv[i], "-p"))
        {
            cout << str << std::endl;
            return false;
        }
            
    }
    for (i=0; i<argc; i++) {
        if (!strcmp(argv[i], str.c_str()))
            return true;
    }
    return false;
}

#define MAKE_IF(name) \
do{ \
	if (if_str_in_argv(argc, argv, #name)) \
    	name(); \
}while(0)

#endif