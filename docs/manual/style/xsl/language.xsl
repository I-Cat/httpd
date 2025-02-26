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

<!DOCTYPE xsl:stylesheet [
    <!ENTITY lf SYSTEM "util/lf.xml">
    <!ENTITY xsl "https://www.w3.org/1999/XSL/Transform">
]>

<xsl:stylesheet version="1.0"
              xmlns:xsl="https://www.w3.org/1999/XSL/Transform"
                  xmlns="">

<xsl:output 
  method="xml"
  encoding="utf-8"
  indent="no"
/>

<xsl:param name="type" />
<xsl:param name="langs" />
<xsl:param name="retired" />

<!-- ==================================================================== -->
<!-- /                                                                    -->
<!-- bootstrap                                                            -->
<!-- ==================================================================== -->
<xsl:template match="/">
<xsl:choose>
<xsl:when test="$type = 'list'">
    <language-list>
    &lf;
    <xsl:call-template name="language-list">
        <xsl:with-param name="langs" select="normalize-space($langs)" />
    </xsl:call-template>
    </language-list>
    &lf;
</xsl:when>
<xsl:otherwise>
    <xsl:apply-templates select="*" />
</xsl:otherwise>
</xsl:choose>
</xsl:template>


<!-- ==================================================================== -->
<!-- language-list                                                        -->
<!-- generate language list                                               -->
<!-- ==================================================================== -->
<xsl:template name="language-list">
<xsl:param name="langs" />

<xsl:if test="string-length($langs)">
    <lang>
        <xsl:value-of select="substring-before(concat($langs, ' '),' ')" />
    </lang>
    &lf;

    <xsl:call-template name="language-list">
        <xsl:with-param name="langs" select="normalize-space(substring-after(
                                             concat($langs, ' '), ' '))" />
    </xsl:call-template>
</xsl:if>
</xsl:template>


<!-- ==================================================================== -->
<!-- <language-list>                                                      -->
<!-- generate stuff from language list                                    -->
<!-- ==================================================================== -->
<xsl:template match="/language-list">
<xsl:choose>
<xsl:when test="$type = 'design'">
    <items>
    &lf;
        <xsl:for-each select="lang">
            <xsl:variable name="file" select="document(concat('../lang/', .,
                                              '.xml'))/language" />
            <item lang="{$file/@id}" charset="{$file/charset}" >
                <xsl:value-of select="$file/target-ext" />
            </item>
            &lf;
        </xsl:for-each>
    </items>
    &lf;
</xsl:when>
<xsl:when test="$type = 'targets'">
    <xsl:apply-templates select="/language-list" mode="targets" />
</xsl:when>
<xsl:when test="$type = 'desc'">
    <xsl:apply-templates select="/language-list" mode="desc" />
</xsl:when>
<xsl:when test="$type = 'modlists'">
    <xsl:apply-templates select="/language-list" mode="modlists" />
</xsl:when>
</xsl:choose>
</xsl:template>


<!-- ==================================================================== -->
<!-- <language-list>                                                      -->
<!-- generate target list from language list                              -->
<!-- ==================================================================== -->
<xsl:template match="/language-list" mode="targets">

<xsl:call-template name="copyright" />

<xsl:call-template name="head">
    <xsl:with-param name="text" select="'this file contains language specific
        targets and will be included'" />
</xsl:call-template>

<xsl:call-template name="head">
    <xsl:with-param name="text" select="'into build.xml. IT IS AUTOGENERATED.
        DO NOT TOUCH!'" />
</xsl:call-template>
<xsl:call-template name="sep" />

<project name="lang-targets">
    &lf;&lf;

    <!-- Test for existence of man page directory -->
    <target name="check-man">
        <if><not><available file="../../man" type="dir" /></not><then>
            <property name="skip-man" value="yes" />
        </then></if>
    </target>
    &lf;

    <!-- build *-all targets -->
    <!-- =================== -->
    <target name="all"
            description="- builds all HTML files and nroff man pages">
        <xsl:attribute name="depends">
            <xsl:text>validate-xml, </xsl:text>
            <xsl:for-each select="lang[document(concat('../lang/', .,
                                       '.xml'))/language/messages]">
                <xsl:value-of select="." />
                <xsl:if test="position() != last()">, </xsl:if>
            </xsl:for-each>
        </xsl:attribute>
    </target>
    &lf;

    <target name="zip-all"
            description="- builds all zip download packages">
        <xsl:attribute name="depends">
            <xsl:for-each select="lang[document(concat('../lang/', .,
                                       '.xml'))/language/messages]">
                <xsl:text>zip-</xsl:text>
                <xsl:value-of select="." />
                <xsl:if test="position() != last()">, </xsl:if>
            </xsl:for-each>
        </xsl:attribute>
    </target>
    &lf;

    <target name="war-all"
            description="- builds all war download packages">
        <xsl:attribute name="depends">
            <xsl:for-each select="lang[document(concat('../lang/', .,
                                       '.xml'))/language/messages]">
                <xsl:text>war-</xsl:text>
                <xsl:value-of select="." />
                <xsl:if test="position() != last()">, </xsl:if>
            </xsl:for-each>
        </xsl:attribute>
    </target>
    &lf;

    <!-- single language targets -->
    <!-- ======================= -->
    <xsl:for-each select="lang">
    <xsl:sort select="." />
        <xsl:variable name="file" select="document(concat('../lang/', .,
                                                   '.xml'))/language" />

        <xsl:if test="$file/messages">
            &lf;
            <xsl:call-template name="head">
                <xsl:with-param name="text" select="$file/name" />
            </xsl:call-template>
            <xsl:call-template name="sep" />

            <property name="inputext.{.}" value="{$file/source-ext}" />&lf;
            <property name="outputext.{.}" value="{$file/target-ext}" />&lf;&lf;

            <target name="{.}" description="- builds {$file/name} HTML files">
                &lf;
                <xsl:text>    </xsl:text>
                <html.generic lang="{.}" />&lf;

                <xsl:if test=". = 'en'">
                    <xsl:text>    </xsl:text><runtarget target="man-en" />&lf;
                </xsl:if>
            </target>
            &lf;

            <target name="-off-{.}" depends="metafiles"
                  unless="-off.{.}.done">&lf;
                <xsl:text>    </xsl:text>
                <dependencies.offline lang="{.}" style="zip" dir="_off" />&lf;
                <xsl:text>    </xsl:text>
                <offline.generic lang="{.}" style="zip" dir="_off" />&lf;
                <xsl:text>    </xsl:text>
                <property name="-off.{.}.done" value="yes" />&lf;
            </target>
            &lf;

            <target name="zip-{.}" depends="-off-{.}"
                    description="- builds the {$file/name} zipped download package">&lf;
                <xsl:text>    </xsl:text>
                <zip.generic lang="{.}" />&lf;
            </target>
            &lf;

            <target name="war-{.}" depends="-off-{.}"
                    description="- builds the {$file/name} Konqueror Web Archive">&lf;
                <xsl:text>    </xsl:text>
                <war.generic lang="{.}" />&lf;
            </target>
            &lf;

            <xsl:if test="$file/chm">
                <target name="chm-{.}"
                        description="- builds the {$file/name} CHM file">&lf;
                    <xsl:text>    </xsl:text>
                    <chm.generic lang="{.}" />&lf;
                </target>
                &lf;
            </xsl:if>

            <xsl:if test="$file/man">
                <target name="man-{.}"
                        depends="check-man"
                        unless="skip-man"
                        description="- builds the {$file/name} nroff files">&lf;
                    <xsl:text>    </xsl:text>
                    <nroff.generic lang="{.}" />&lf;
                </target>
                &lf;
            </xsl:if>

            <xsl:if test=". = 'en'">
                <target name="latex-en"
                        description="- builds the English latex file">&lf;
                    <xsl:text>    </xsl:text>
                    <latex.generic lang="en" />&lf;
                </target>
                &lf;
            </xsl:if>
        </xsl:if>
    </xsl:for-each>
    &lf;

    <!-- XML validation -->
    <!-- ============== -->
    <xsl:call-template name="head">
        <xsl:with-param name="text" select="'XML validation.'" />
    </xsl:call-template>
    <xsl:call-template name="head">
        <xsl:with-param name="text" select="'If you get an error during
            transformation, this task may be useful'" />
    </xsl:call-template>
    <xsl:call-template name="head">
        <xsl:with-param name="text" select="'because it mostly gives you a
            hint, where you forgot the &lt;/p&gt; ;-)'" />
    </xsl:call-template>
    <xsl:call-template name="sep" />

    <target name="validate-xml" description="- validates all XML source files">
        &lf;
        <xsl:text>    </xsl:text>
        <xmlvalidate lenient="false" failonerror="true" warn="true">
            &lf;
            <xsl:text>        </xsl:text>
            <xmlcatalog refid="w3c-catalog" />&lf;
            <xsl:text>        </xsl:text>
            <fileset dir="../">&lf;
                <xsl:for-each select="lang">
                <xsl:sort select="." />

                    <xsl:variable name="file" select="document(concat(
                                                      '../lang/', ., '.xml'))
                                                      /language" />
                    <xsl:if test="$file/messages">
                        <xsl:text>            </xsl:text>
                        <include name="**/*{$file/source-ext}" />&lf;
                    </xsl:if>
                </xsl:for-each>
                &lf;
                <xsl:text>            </xsl:text>
                <patternset refid="excludes" />&lf;
                <xsl:text>            </xsl:text>
                <patternset refid="scratch" />&lf;
            <xsl:text>        </xsl:text>
            </fileset>
            &lf;
        <xsl:text>    </xsl:text>
        </xmlvalidate>
        &lf;
    </target>
    &lf;&lf;
</project>
</xsl:template>


<!-- ==================================================================== -->
<!-- <language-list>                                                      -->
<!-- generate list of modulelists                                         -->
<!-- ==================================================================== -->
<xsl:template match="/language-list" mode="modlists">
<items>
    &lf;
    <xsl:for-each select="lang">
    <xsl:sort select="." />

        <xsl:variable name="file" select="document(concat(
                                          '../lang/', ., '.xml'))
                                          /language" />
        <item lang="{.}">
            <xsl:text>../../../mod/allmodules</xsl:text>
            <xsl:value-of select="$file/source-ext" />
        </item>
        &lf;
    </xsl:for-each>
</items>
</xsl:template>

<!-- ==================================================================== -->
<!-- <language-list>                                                      -->
<!-- generate project description                                         -->
<!-- ==================================================================== -->
<xsl:template match="/language-list" mode="desc">

<xsl:call-template name="copyright" />

<description><xsl:text>
This build file contains all operations that are necessary for building
the Apache httpd documentation. It is called by invoking build.bat (Win32)
or build.sh (/bin/sh systems) with a target argument (full list below).
For example, if you want to build the Japanese HTML files, type:

  ./build.sh ja

Some targets have additional requirements:

* 'metafiles' and 'modulelists' need perl in PATH. (It's checked automatically
  and skipped if perl is not available)

* 'chm-foo' targets need:
  - the HTML Help compiler in PATH (or modify this build file). The
    compiler (hhc.exe) is part of the HTML Help Workshop which is freely
    available and can be downloaded from
    https://msdn.microsoft.com/en-us/library/windows/desktop/ms669985%28v=vs.85%29.aspx
  - The appropriate locale (e.g. Japanese) before invoking hhc.exe. Otherwise
    the compiler is not able to build the fulltext search index correctly and
    the TOC may be garbled, too. In particular:
</xsl:text>

    <xsl:for-each select="lang">
    <xsl:sort select="." />

        <xsl:variable name="file" select="document(concat('../lang/', .,
                                          '.xml'))/language" />

        <xsl:if test="$file/messages and $file/chm">
            <xsl:text>    + chm-</xsl:text>
            <xsl:value-of select="." />
            <xsl:text>: </xsl:text>
            <xsl:value-of select="normalize-space($file/chm/settings)" />
            &lf;
        </xsl:if>
    </xsl:for-each>
    &lf;
</description>
</xsl:template>


<!-- ==================================================================== -->
<!-- <language>                                                           -->
<!-- generate language specific xslt                                      -->
<!-- ==================================================================== -->
<xsl:template match="/language">

<xsl:call-template name="copyright" />

<xsl:element name="xsl:stylesheet" namespace="&xsl;">
    <xsl:attribute name="version">1.0</xsl:attribute>
    &lf;
    &lf;

    <xsl:element name="xsl:output">
        <xsl:attribute name="method">
            <xsl:choose>
            <xsl:when test="$type = 'manual' or
                            $type = 'chm' or
                            $type = 'zip'">
                <xsl:text>xml</xsl:text>
            </xsl:when>
            <xsl:when test="$type = 'hhc' or
                            $type = 'hhp' or
                            $type = 'man'">
                <xsl:text>text</xsl:text>
            </xsl:when>
            <xsl:otherwise>
                <xsl:message terminate="yes">
                    <xsl:text>Unknown style type '</xsl:text>
                    <xsl:value-of select="$type" />
                    <xsl:text>'!</xsl:text>
                </xsl:message>
            </xsl:otherwise>
            </xsl:choose>
        </xsl:attribute>
        <xsl:attribute name="encoding">
            <xsl:choose>
            <xsl:when test="$type = 'chm' or
                            $type = 'hhc' or
                            $type = 'hhp'">
                <xsl:value-of select="chm/charset" />
            </xsl:when>
            <xsl:when test="$type = 'man'">
                <xsl:value-of select="man/charset" />
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="charset" />
            </xsl:otherwise>
            </xsl:choose>
        </xsl:attribute>
        <xsl:attribute name="indent">no</xsl:attribute>
        <xsl:if test="$type = 'manual' or
                      $type = 'chm' or
                      $type = 'zip'">
            <xsl:attribute name="doctype-public">
                <xsl:text>-//W3C//DTD XHTML 1.0 Strict//EN</xsl:text>
            </xsl:attribute>
        </xsl:if>
        <xsl:if test="$type = 'manual'">
            <xsl:attribute name="doctype-system">
                <xsl:text>https://www.w3.org/TR/xhtml1/DTD/</xsl:text>
                <xsl:text>xhtml1-strict.dtd</xsl:text>
            </xsl:attribute>
        </xsl:if>
        <xsl:if test="$type = 'chm' or
                      $type = 'zip'">
            <xsl:attribute name="omit-xml-declaration">yes</xsl:attribute>
        </xsl:if>
    </xsl:element>
    &lf;&lf;

    <xsl:comment>
        <xsl:text> Read the localized messages from the specified </xsl:text>
        <xsl:text>language file </xsl:text>
    </xsl:comment>
    &lf;

    <xsl:element name="xsl:variable">
        <xsl:attribute name="name">message</xsl:attribute>
        <xsl:attribute name="select">
            <xsl:text>document('</xsl:text>
            <xsl:if test="$type != 'manual'">../</xsl:if>
            <xsl:text>lang/</xsl:text>
            <xsl:value-of select="@id" />
            <xsl:text>.xml')/language/messages/message</xsl:text>
        </xsl:attribute>
    </xsl:element>
    &lf;

    <xsl:if test="$type != 'man'">
        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">doclang</xsl:attribute>
            <xsl:value-of select="@id" />
        </xsl:element>
        &lf;
        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">allmodules</xsl:attribute>
            <xsl:attribute name="select">
                <xsl:text>document('</xsl:text>
                <xsl:if test="$type != 'manual'">../</xsl:if>
                <xsl:text>xsl/util/allmodules.xml')</xsl:text>
                <xsl:text>/items/item[@lang=$doclang]</xsl:text>
            </xsl:attribute>
        </xsl:element>
        &lf;
    </xsl:if>
    &lf;

    <xsl:if test="$type != 'man'">
        <xsl:comment>
            <xsl:text> some meta information have to be passed to </xsl:text>
            <xsl:text>the transformation </xsl:text>
        </xsl:comment>
        &lf;
    </xsl:if>

    <xsl:if test="$type = 'manual' or
                  $type = 'chm' or
                  $type = 'zip' or
                  $type = 'hhc'">
        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">output-encoding</xsl:attribute>
            <xsl:choose>
            <xsl:when test="$type = 'chm' or
                             $type = 'hhc'">
                <xsl:value-of select="normalize-space(chm/charset)" />
            </xsl:when>
            <xsl:otherwise>
                <xsl:value-of select="normalize-space(charset)" />
            </xsl:otherwise>
            </xsl:choose>
        </xsl:element>
        &lf;
    </xsl:if>

    <xsl:if test="$type = 'manual' or
                  $type = 'chm' or
                  $type = 'zip'">
        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">is-chm</xsl:attribute>
            <xsl:attribute name="select">
                <xsl:choose>
                <xsl:when test="$type = 'chm'">
                    <xsl:text>true()</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text>false()</xsl:text>
                </xsl:otherwise>
                </xsl:choose>
            </xsl:attribute>
        </xsl:element>
        &lf;

        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">is-zip</xsl:attribute>
            <xsl:attribute name="select">
                <xsl:choose>
                <xsl:when test="$type = 'zip'">
                    <xsl:text>true()</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text>false()</xsl:text>
                </xsl:otherwise>
                </xsl:choose>
            </xsl:attribute>
        </xsl:element>
        &lf;

        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">is-retired</xsl:attribute>
            <xsl:attribute name="select">
                <xsl:choose>
                <xsl:when test="$retired = 'yes'">
                    <xsl:text>true()</xsl:text>
                </xsl:when>
                <xsl:otherwise>
                    <xsl:text>false()</xsl:text>
                </xsl:otherwise>
                </xsl:choose>
            </xsl:attribute>
        </xsl:element>
        &lf;&lf;
    </xsl:if>

    <xsl:if test="$type = 'hhc'">
        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">toc-font</xsl:attribute>
            <xsl:value-of select="normalize-space(chm/toc-font)" />
        </xsl:element>
        &lf;

        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">xml-ext</xsl:attribute>
            <xsl:value-of select="normalize-space(source-ext)" />
        </xsl:element>
        &lf;&lf;
    </xsl:if>

    <xsl:if test="$type = 'hhp'">
        <xsl:element name="xsl:variable">
            <xsl:attribute name="name">hhp-lang</xsl:attribute>
            <xsl:value-of select="normalize-space(chm/lang)" />
        </xsl:element>
        &lf;&lf;
    </xsl:if>

    <xsl:comment> Now get the real guts of the stylesheet </xsl:comment>
    &lf;

    <xsl:element name="xsl:include">
        <xsl:attribute name="href">
            <xsl:choose>
            <xsl:when test="$type = 'chm' or
                            $type = 'zip'">
                <xsl:text>../xsl/common.xsl</xsl:text>
            </xsl:when>
            <xsl:when test="$type = 'hhc'">
                <xsl:text>../xsl/hhc.xsl</xsl:text>
            </xsl:when>
            <xsl:when test="$type = 'hhp'">
                <xsl:text>../xsl/hhp.xsl</xsl:text>
            </xsl:when>
            <xsl:when test="$type = 'man'">
                <xsl:text>../xsl/nroff.xsl</xsl:text>
            </xsl:when>
            <xsl:otherwise>
                <xsl:text>xsl/common.xsl</xsl:text>
            </xsl:otherwise>
            </xsl:choose>
        </xsl:attribute>
    </xsl:element>
    &lf;&lf;
</xsl:element>

</xsl:template>
<!-- /language -->


<xsl:template name="copyright">
&lf;
<xsl:comment><xsl:text>
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
</xsl:text></xsl:comment>
&lf;&lf;
</xsl:template>


<xsl:template name="sepstring">
<xsl:text>============================================</xsl:text>
<xsl:text>========================</xsl:text>
</xsl:template>


<xsl:template name="sep">
<xsl:comment>
    <xsl:text> </xsl:text>
    <xsl:call-template name="sepstring" />
    <xsl:text> </xsl:text>
</xsl:comment>
&lf;
</xsl:template>

<xsl:template name="head">
<xsl:param name="text" />

<xsl:variable name="s"><xsl:call-template name="sepstring" /></xsl:variable>
<xsl:variable name="empty" select="translate($s, '=', ' ')" />

<xsl:comment>
    <xsl:text> </xsl:text>
    <xsl:value-of select="substring(concat(normalize-space($text), $empty), 1,
                                    string-length($empty))" />
    <xsl:text> </xsl:text>
</xsl:comment>
&lf;
</xsl:template>

</xsl:stylesheet>
