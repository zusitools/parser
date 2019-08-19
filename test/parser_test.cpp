#include "zusi_parser/zusi_parser.hpp"

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(ZusiParserTest)

BOOST_AUTO_TEST_CASE(Anfuehrungszeichen) {
  const auto result = zusixml::parse_root<Zusi>(R""(<?xml version="1.0" encoding="UTF-8"?>
<Zusi>
<Info DateiTyp="author" Version="A.1" MinVersion="A.1">
<AutorEintrag AutorID="12345" AutorName="Test '1'"/>
<AutorEintrag AutorID='12346' AutorName='Test "2"'/>
</Info>
<author/>
</Zusi>)"");
  BOOST_TEST_REQUIRE(static_cast<bool>(result));
  BOOST_TEST_REQUIRE(static_cast<bool>(result->Info));
  BOOST_TEST_REQUIRE(result->Info->children_AutorEintrag.size() == 2);
  BOOST_TEST_REQUIRE(static_cast<bool>(result->Info->children_AutorEintrag[0]));
  BOOST_TEST_REQUIRE(static_cast<bool>(result->Info->children_AutorEintrag[1]));

  BOOST_TEST(result->Info->children_AutorEintrag[0]->AutorID == 12345);
  BOOST_TEST(result->Info->children_AutorEintrag[0]->AutorName == "Test '1'");
  BOOST_TEST(result->Info->children_AutorEintrag[1]->AutorID == 12346);
  BOOST_TEST(result->Info->children_AutorEintrag[1]->AutorName == "Test \"2\"");
}

BOOST_AUTO_TEST_CASE(XMLEntities) {
  const auto result = zusixml::parse_root<Zusi>(R""(<?xml version="1.0" encoding="UTF-8"?>
<Zusi>
<Info DateiTyp="author" Version="A.1" MinVersion="A.1">
<AutorEintrag AutorName="Test &lt;&apos;1&apos&gt;&amp;apos;"/>
<AutorEintrag AutorName='Test &lt;&quot;2&quot&gt;&amp;quot;'/>
</Info>
<author/>
</Zusi>)"");
  BOOST_TEST_REQUIRE(static_cast<bool>(result));
  BOOST_TEST_REQUIRE(static_cast<bool>(result->Info));
  BOOST_TEST_REQUIRE(result->Info->children_AutorEintrag.size() == 2);
  BOOST_TEST_REQUIRE(static_cast<bool>(result->Info->children_AutorEintrag[0]));
  BOOST_TEST_REQUIRE(static_cast<bool>(result->Info->children_AutorEintrag[1]));

  BOOST_TEST(result->Info->children_AutorEintrag[0]->AutorName == "Test <'1&apos>&apos;");
  BOOST_TEST(result->Info->children_AutorEintrag[1]->AutorName == "Test <\"2&quot>&quot;");
}

BOOST_AUTO_TEST_SUITE_END()
