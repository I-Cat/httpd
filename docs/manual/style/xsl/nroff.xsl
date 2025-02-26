<?xml version="1.0"?>

<!--
 Licensed to the Apache Software Foundation (ASF) under one or more
 contributor license agreements.  See the NOTICE file distributed with
 this work for additional information regarding copyright ownership.
 The ASF licenses this file to You under the Apache License, Version 2.0
 (the "License"); you may not use this file except in compliance with
 the License.  You may obtain a copy of the License at

     https://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
-->

<!--
 * This software is based on initial work of Joe Orton <jorton redhat.com>
 * (contributed to the ASF) which is based on the db2man stylesheets developed
 * by Martijn van Beers. db2man is now part of docbook-xsl, which is
 * licensed under the following terms:
 *
 * Copyright
 * =========
 *
 * Copyright (C) 1999, 2000, 2001, 2002 Norman Walsh
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the ``Software''), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * Except as contained in this notice, the names of individuals
 * credited with contribution to this software shall not be used in
 * advertising or otherwise to promote the sale, use or other
 * dealings in this Software without prior written authorization
 * from the individuals in question.
 *
 * Any stylesheet derived from this Software that is publicly
 * distributed will be identified with a different name and the
 * version strings in any derived Software will be changed so that
 * no possibility of confusion between the derived package and this
 * Software will exist.
 *
 * Warranty
 * ========
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL NORMAN WALSH OR ANY OTHER
 * CONTRIBUTOR BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */ -->

<!DOCTYPE xsl:stylesheet [
    <!ENTITY lf SYSTEM "../xsl/util/lf.xml">
]>
<xsl:stylesheet version="1.0"
              xmlns:xsl="https://www.w3.org/1999/XSL/Transform">

<!--                                                                      -->
<!-- params injected from elsewhere                                       -->
<!--                                                                      -->
<xsl:param name="section" select="'1'" />
<xsl:param name="date" />

<!-- Constants used for case translation -->
<xsl:variable name="lowercase" select="$message[@id='lowercase']" />
<xsl:variable name="uppercase" select="$message[@id='uppercase']" />

<!-- ==================================================================== -->
<!-- <manualpage>                                                         -->
<!-- Process an entire document into an nroff formatted man page          -->
<!-- ==================================================================== -->
<xsl:template match="manualpage">
<!-- start at the beginning ;-) -->
<xsl:text>.\" XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX</xsl:text>&lf;
<xsl:text>.\" DO NOT EDIT! Generated from XML source.</xsl:text>&lf;
<xsl:text>.\" XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX</xsl:text>&lf;
<xsl:text>.de Sh \" Subsection</xsl:text>&lf;
<xsl:text>.br</xsl:text>&lf;
<xsl:text>.if t .Sp</xsl:text>&lf;
<xsl:text>.ne 5</xsl:text>&lf;
<xsl:text>.PP</xsl:text>&lf;
<xsl:text>\fB\\$1\fR</xsl:text>&lf;
<xsl:text>.PP</xsl:text>&lf;
<xsl:text>..</xsl:text>&lf;
<xsl:text>.de Sp \" Vertical space (when we can't use .PP)</xsl:text>&lf;
<xsl:text>.if t .sp .5v</xsl:text>&lf;
<xsl:text>.if n .sp</xsl:text>&lf;
<xsl:text>..</xsl:text>&lf;
<xsl:text>.de Ip \" List item</xsl:text>&lf;
<xsl:text>.br</xsl:text>&lf;
<xsl:text>.ie \\n(.$>=3 .ne \\$3</xsl:text>&lf;
<xsl:text>.el .ne 3</xsl:text>&lf;
<xsl:text>.IP "\\$1" \\$2</xsl:text>&lf;
<xsl:text>..</xsl:text>&lf;

<!-- reftitle -->
<xsl:text>.TH "</xsl:text>
<!-- standard man page width is 64 chars; 6 chars needed for the two
    (x) volume numbers, and 2 spaces, leaves 56 -->
<xsl:value-of select="substring(translate(substring-before(
    normalize-space(title), ' - '), $lowercase, $uppercase), 1, (56 -
    string-length(substring-before(normalize-space(title), ' - '))) div 2)" />
<xsl:text>" </xsl:text>

<!-- section -->
<xsl:value-of select="$section" />
<xsl:text> "</xsl:text>

<!-- date; perhaps injected via ant later. -->
<xsl:value-of select="$date" />
<xsl:text>" "</xsl:text>

<!-- productname -->
<xsl:value-of select="concat($message[@id='apache'], ' ', $message[@id='http-server'])" />
<xsl:text>" "</xsl:text>

<!-- title -->
<xsl:value-of select="substring-before(normalize-space(title), ' - ')" />
<xsl:text>"</xsl:text>&lf;

<!-- reorder the paragraphs a bit -->
<xsl:apply-templates select="title" />&lf;
<xsl:apply-templates select="section[@id='synopsis']" />&lf;
<xsl:apply-templates select="summary" />&lf;
<xsl:apply-templates select="section[@id!='synopsis']" />&lf;
</xsl:template>
<!-- /manualpage -->


<!-- ==================================================================== -->
<!-- <manualpage><title>                                                  -->
<!-- Process heading                                                      -->
<!-- ==================================================================== -->
<xsl:template match="manualpage/title">
<xsl:value-of select="$message[@id='hyphenation']" />&lf;
<xsl:value-of select="concat('.SH ', $message[@id='name-section'])" />&lf;
<xsl:variable name="text">
    <xsl:call-template name="filter.escape">
        <xsl:with-param name="text" select="normalize-space(.)" />
    </xsl:call-template>
</xsl:variable>
<xsl:value-of select="substring-before($text, ' - ')" />
<xsl:text> \- </xsl:text>
<xsl:value-of select="substring-after($text, ' - ')" />
</xsl:template>
<!-- /manualpage/title -->


<!-- ==================================================================== -->
<!-- <p>                                                                  -->
<!-- Process paragraph                                                    -->
<!-- ==================================================================== -->
<xsl:template match="p">
&lf;
<xsl:text>.PP</xsl:text>&lf;

<xsl:for-each select="node()">
    <xsl:choose>
    <xsl:when test="self::text()">
	    <xsl:if test="starts-with(translate(., '&#10;', ' '), ' ') and
		              preceding-sibling::node()[name(.) != '']">
	        <xsl:text> </xsl:text>
	    </xsl:if>
        <xsl:variable name="content">
	        <xsl:apply-templates select="." />
	    </xsl:variable>
	    <xsl:value-of select="normalize-space($content)"/>
	    <xsl:if test="translate(substring(., string-length(.), 1), '&#10;',
                      ' ') = ' ' and following-sibling::node()[name(.) != '']">
	        <xsl:text> </xsl:text>
	    </xsl:if>
    </xsl:when>
    <xsl:otherwise>
        <xsl:variable name="content">
            <xsl:apply-templates select="." />
        </xsl:variable>
        <xsl:value-of select="normalize-space($content)"/>
    </xsl:otherwise>
    </xsl:choose>
</xsl:for-each>&lf;
</xsl:template>
<!-- /p -->


<!-- ==================================================================== -->
<!-- <section>                                                            -->
<!-- process main section                                                 -->
<!-- ==================================================================== -->
<xsl:template match="section">
&lf;
<xsl:text>.SH "</xsl:text>
    <xsl:call-template name="filter.escape">
        <xsl:with-param name="text"
            select="normalize-space(translate(title, $lowercase, $uppercase))"/>
    </xsl:call-template>
<xsl:text>"</xsl:text>&lf;
<xsl:apply-templates />
</xsl:template>
<xsl:template match="section/title" />
<!-- /section -->


<!-- ==================================================================== -->
<!-- <section><section>                                                   -->
<!-- process subsection                                                   -->
<!-- ==================================================================== -->
<xsl:template match="section/section">
&lf;
<xsl:text>.SS "</xsl:text>
    <xsl:call-template name="filter.escape">
        <xsl:with-param name="text" select="normalize-space(title)"/>
    </xsl:call-template>
<xsl:text>"</xsl:text>&lf;
<xsl:apply-templates />
</xsl:template>
<!-- /section/section -->


<!-- ==================================================================== -->
<!-- <summary>                                                            -->
<!-- process summary section                                              -->
<!-- ==================================================================== -->
<xsl:template match="summary">
&lf;
<xsl:text>.SH "</xsl:text>
    <xsl:call-template name="filter.escape">
        <xsl:with-param name="text" select="normalize-space(translate($message
                          [@id='summary'], $lowercase, $uppercase))"/>
    </xsl:call-template>
<xsl:text>"</xsl:text>&lf;
<xsl:apply-templates />
</xsl:template>
<!-- /summary -->


<!-- ==================================================================== -->
<!-- <var>, <em>                                                          -->
<!-- show it somewhat special (italic)                                    -->
<!-- ==================================================================== -->
<xsl:template match="var|em">
<xsl:text>\fI</xsl:text>
    <xsl:apply-templates />
<xsl:text>\fR</xsl:text>
</xsl:template>


<!-- ==================================================================== -->
<!-- <strong>                                                             -->
<!-- show it somewhat special (bold)                                      -->
<!-- ==================================================================== -->
<xsl:template match="strong|code">
<xsl:text>\fB</xsl:text>
    <xsl:apply-templates />
<xsl:text>\fR</xsl:text>
</xsl:template>


<!-- ==================================================================== -->
<!-- <directive>                                                          -->
<!-- ==================================================================== -->
<xsl:template match="directive">
<xsl:if test="@type = 'section'">&lt;</xsl:if>
<xsl:apply-templates />
<xsl:if test="@type = 'section'">&gt;</xsl:if>
</xsl:template>


<!-- ==================================================================== -->
<!-- <dl>                                                                 -->
<!-- ==================================================================== -->
<xsl:template match="dl">
&lf;
<xsl:apply-templates />&lf;
</xsl:template>


<!-- ==================================================================== -->
<!-- <dt>                                                                 -->
<!-- ==================================================================== -->
<xsl:template match="dt">
&lf;
<xsl:text>.TP</xsl:text>&lf;

<xsl:variable name="dt-content">
    <xsl:apply-templates />
</xsl:variable>
<xsl:value-of select="normalize-space($dt-content)" />&lf;

<xsl:variable name="dd-content">
    <xsl:apply-templates
        select="following-sibling::dd[position() = 1]/node()" />
</xsl:variable>
<xsl:value-of select="normalize-space($dd-content)" />
</xsl:template>
<!-- /dt -->


<!-- ==================================================================== -->
<!-- <example>                                                            -->
<!-- ==================================================================== -->
<xsl:template match="example[ancestor::dd]">
  <xsl:apply-templates/>
</xsl:template>

<xsl:template match="example[not(ancestor::dd)]">
&lf;
<xsl:text>.nf</xsl:text>&lf;
<xsl:apply-templates />&lf;
<xsl:text>.fi</xsl:text>&lf;
</xsl:template>
<!-- /example -->


<!-- ==================================================================== -->
<!-- simple table support ... (exactly 2 columns)                         -->
<!-- ==================================================================== -->
<xsl:template match="table/tr">
<xsl:if test="count(./*) &gt; 2">
    <xsl:message terminate="yes">
FATAL: only tables with two (2) columns are supported.
    </xsl:message>
</xsl:if>

&lf;
<xsl:text>.Ip "\(bu \s-1</xsl:text>
<xsl:variable name="first-content">
   <xsl:apply-templates select="./*[1]/node()" />
</xsl:variable>
<xsl:value-of select="normalize-space($first-content)" />

<xsl:text>\s0 \- </xsl:text>
<xsl:variable name="second-content">
   <xsl:apply-templates select="./*[2]/node()" />
</xsl:variable>
<xsl:value-of select="normalize-space($second-content)" />&lf;
</xsl:template>
<!-- /table/tr -->


<!-- ==================================================================== -->
<!-- text filter                                                          -->
<!-- ==================================================================== -->
<xsl:template match="text()">
<xsl:choose>
<xsl:when test="normalize-space(.) != ''">
<xsl:call-template name="filter.escape">
    <xsl:with-param name="text" select="." />
</xsl:call-template>
</xsl:when>
<xsl:otherwise>
    <xsl:text> </xsl:text>
</xsl:otherwise>
</xsl:choose>
</xsl:template>


<!-- ==================================================================== -->
<!-- pass through content                                                 -->
<!-- ==================================================================== -->
<xsl:template match="a|module|table|program|glossary">
<xsl:apply-templates />
</xsl:template>


<!-- ==================================================================== -->
<!-- remove some stuff from the output                                    -->
<!-- ==================================================================== -->
<xsl:template match="parentdocument|seealso|dd|td|example/br|dd/br" />


<!-- ==================================================================== -->
<!-- the rest will be rejected                                            -->
<!-- ==================================================================== -->
<xsl:template match="*">
<xsl:message terminate="yes">
FATAL: the behaviour of the &lt;<xsl:value-of select="local-name()" />&gt;
element was not tested. You need to modify
the XSLT stylesheet in order to get it work.
</xsl:message>
</xsl:template>


<!-- ==================================================================== -->
<!-- filter.escape                                                        -->
<!-- escape special characters                                            -->
<!-- ==================================================================== -->
<xsl:template name="filter.escape">
<xsl:param name="text" />

<xsl:choose>
<xsl:when test="contains($text, '\')">
    <xsl:call-template name="filter.escape.period">
        <xsl:with-param name="text" select="substring-before($text, '\')" />
    </xsl:call-template>
    <xsl:text>\\</xsl:text>
    <xsl:call-template name="filter.escape">
        <xsl:with-param name="text" select="substring-after($text, '\\')" />
    </xsl:call-template>
</xsl:when>
<xsl:otherwise>
    <xsl:call-template name="filter.escape.period">
        <xsl:with-param name="text" select="$text" />
    </xsl:call-template>
</xsl:otherwise>
</xsl:choose>
</xsl:template>
<!-- /filter.escape -->


<!-- ==================================================================== -->
<!-- filter.period                                                        -->
<!-- accompanying template to filter.escape                               -->
<!-- ==================================================================== -->
<xsl:template name="filter.escape.period">
<xsl:param name="text" />

<xsl:choose>
<xsl:when test="contains($text, '.')"><!-- period replaced by \&. -->
    <xsl:value-of select="substring-before($text, '.')" />
    <xsl:text>\&amp;.</xsl:text>
    <xsl:call-template name="filter.escape.period">
        <xsl:with-param name="text" select="substring-after($text, '.')" />
    </xsl:call-template>
</xsl:when>
<xsl:otherwise>
    <xsl:value-of select="$text" />
</xsl:otherwise>
</xsl:choose>
</xsl:template>
<!-- /filter.escape.period -->

</xsl:stylesheet>
