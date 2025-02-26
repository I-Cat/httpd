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
]>
<xsl:stylesheet version="1.0"
              xmlns:xsl="https://www.w3.org/1999/XSL/Transform"
                  xmlns="https://www.w3.org/1999/xhtml">

<!-- ==================================================================== -->
<!-- <moduleindex>                                                        -->
<!-- Builds the moduleindex page                                          -->
<!-- ==================================================================== -->
<xsl:template match="moduleindex">
<html xml:lang="{$doclang}" lang="{$doclang}">
    <xsl:call-template name="head"/>&lf;

    <body id="module-index">
        <xsl:call-template name="top"/>  

        <div id="page-content">
            <xsl:call-template name="retired" />

            <div id="preamble">
                <h1>
                    <xsl:value-of select="title" />
                </h1>&lf;

                <xsl:call-template name="langavail" />&lf;

                <xsl:apply-templates select="summary" />
            </div>&lf; <!-- /#preamble -->

            <xsl:if test="not($is-chm) or seealso">
                <div id="quickview">
                    <xsl:if test="not($is-chm)">
                        <ul id="toc">&lf;
                        <li>
                            <img src="{$path}/images/down.gif" alt="" />
                            <xsl:text> </xsl:text>
                            <a href="#core">
                                <xsl:value-of select="$message
                                                      [@id='corefeatures']" />
                            </a>
                        </li>
                        <li>
                            <img src="{$path}/images/down.gif" alt="" />
                            <xsl:text> </xsl:text>
                            <a href="#other">
                                <xsl:value-of select="$message
                                                      [@id='othermodules']" />
                            </a>
                        </li>
                        </ul>
                    </xsl:if> <!-- !$is-chm -->

                    <xsl:if test="seealso">
                        <h3>
                            <xsl:value-of select="$message
                                                  [@id='seealso']" />
                        </h3>&lf;            

                        <ul class="seealso">&lf;
                        <xsl:for-each select="seealso">
                            <li>
                                <xsl:apply-templates />
                            </li>&lf;
                        </xsl:for-each>
                        </ul>
                    </xsl:if> <!-- /seealso -->
                </div> <!-- /#quickview -->
            </xsl:if>&lf; <!-- have sidebar -->

            <xsl:call-template name="toplink" />&lf;

            <div class="section">
                <h2>
                    <a name="core" id="core">
                        <xsl:value-of select="$message
                                              [@id='corefeatures']" />
                    </a>
                </h2>&lf;

                <!-- core -->
                <dl>&lf;
                <dt>
                    <a href="{document(document($allmodules)/modulefilelist
                              /modulefile[starts-with(., 'core.xml')])
                              /modulesynopsis/name}.html">
                        <xsl:value-of
                            select="document(document($allmodules)/modulefilelist
                                    /modulefile[starts-with(., 'core.xml')])
                                    /modulesynopsis/name" />
                    </a>
                </dt>
                <dd>
                    <xsl:apply-templates
                        select="document(document($allmodules)/modulefilelist
                                         /modulefile[starts-with(., 'core.xml')])
                                /modulesynopsis/description" />
                </dd>&lf;

                <!-- mpm_common -->
                <dt>
                    <a href="{document(document($allmodules)/modulefilelist
                                       /modulefile[starts-with(., 'mpm_common.xml')])
                              /modulesynopsis/name}.html">
                        <xsl:value-of
                            select="document(document($allmodules)/modulefilelist
                                             /modulefile[starts-with(., 'mpm_common.xml')])
                                    /modulesynopsis/name" />
                    </a>
                </dt>
                <dd class="separate">
                    <xsl:apply-templates
                        select="document(document($allmodules)/modulefilelist
                                         /modulefile[starts-with(., 'mpm_common.xml')])
                                /modulesynopsis/description" />
                </dd>&lf;

                <!-- and now the remaining MPMs -->
                <xsl:variable name="mpmmodules"
                    select="document(document($allmodules)/modulefilelist/modulefile)
                            /modulesynopsis
                                [status='MPM' and name != 'mpm_common']" />
                <xsl:variable name="translist">
                    <xsl:call-template name="module-translist">
                        <xsl:with-param name="modules" select="$mpmmodules" />
                    </xsl:call-template>
                </xsl:variable>

                <xsl:for-each select="$mpmmodules">
                <xsl:sort select="substring-before(substring-after($translist,
                    concat('- ', translate(normalize-space(name), $lowercase,
                    $uppercase), ' ')), ' -')" />

                    <dt>
                        <a href="{name}.html">
                            <xsl:value-of select="name" />
                        </a>
                    </dt>
                    <dd>
                        <xsl:apply-templates select="description" />
                    </dd>&lf;
                </xsl:for-each>
                </dl>
            </div>&lf; <!-- /core section -->

            <xsl:call-template name="toplink" />&lf;

            <div class="section">
                <h2>
                    <a name="other" id="other">
                        <xsl:value-of select="$message
                                              [@id='othermodules']" />
                    </a>
                </h2>&lf;
            
                <xsl:variable name="modules"
                    select="document(document($allmodules)/modulefilelist/modulefile)
                            /modulesynopsis[status!='MPM' and
                                            status!='Core']" />

                <xsl:variable name="start-letters">
                    <xsl:call-template name="module-startletters">
                        <xsl:with-param name="modules" select="$modules" />
                    </xsl:call-template>
                </xsl:variable>

                <p class="letters">
                    <xsl:call-template name="letter-bar">
                        <xsl:with-param name="letters" select="$start-letters"/>
                        <xsl:with-param name="first" select="true()"/>
                    </xsl:call-template>
                </p>&lf;

                <dl>
                <xsl:call-template name="mindex-of-letter">
                    <xsl:with-param name="letters-todo"
                        select="$start-letters" />
                    <xsl:with-param name="modules" select="$modules" />
                </xsl:call-template>
                </dl>
            </div> <!-- /modules section -->
        </div>&lf; <!-- /#page-content -->

        <xsl:call-template name="bottom" />&lf;
    </body>
</html>
</xsl:template> 
<!-- /moduleindex -->


<!-- ==================================================================== -->
<!-- the working horse. builds list items of all modules starting with    -->
<!-- one letter when done, it calls itself to catch the next letter       -->
<!-- ==================================================================== -->
<xsl:template name="mindex-of-letter">
<xsl:param name="letters-todo"/>
<xsl:param name="modules"/>

<xsl:variable name="letter" select="substring($letters-todo, 1, 1)" />
<xsl:variable name="translist">
    <xsl:call-template name="module-translist">
        <xsl:with-param name="modules" select="$modules" />
    </xsl:call-template>
</xsl:variable>

<xsl:for-each
    select="$modules[$letter=substring(substring-before(substring-after(
        $translist, concat('- ', translate(normalize-space(name), $lowercase,
        $uppercase), ' ')), ' -'), 1, 1)]">
<xsl:sort
    select="substring-before(substring-after($translist, concat('- ',
        translate(normalize-space(name),$lowercase,$uppercase), ' ')), ' -')" />

    <dt>
        <a href="{name}.html">
            <xsl:if test="position() = 1">
                <xsl:attribute name="id">
                    <xsl:value-of select="$letter" />
                </xsl:attribute>
                <xsl:attribute name="name">
                    <xsl:value-of select="$letter" />
                </xsl:attribute>
            </xsl:if>

            <xsl:value-of select="name" />
        </a>
    </dt>
    <dd>
        <xsl:apply-templates select="description" />
    </dd>&lf;
</xsl:for-each> <!-- /directives -->

<!-- call next letter, if there is -->
<xsl:if test="string-length($letters-todo) &gt; 1">
    <xsl:call-template name="mindex-of-letter">
        <xsl:with-param name="letters-todo"
            select="substring($letters-todo, 2)" />
        <xsl:with-param name="modules" select="$modules" />
    </xsl:call-template>
</xsl:if>
</xsl:template>
<!-- /mindex-of-letter -->


<!-- ==================================================================== -->
<!-- collect start letters of modules                                     -->
<!-- ==================================================================== -->
<xsl:template name="module-startletters">
<xsl:param name="modules" />

<xsl:variable name="translist">
    <xsl:call-template name="module-translist">
        <xsl:with-param name="modules" select="$modules" />
    </xsl:call-template>
</xsl:variable>

<xsl:call-template name="_squeeze-letters">
    <xsl:with-param name="lastletter" select="''" />
    <xsl:with-param name="letters">
        <xsl:for-each select="$modules">
        <xsl:sort
            select="substring-before(substring-after($translist, concat('- ',
                translate(normalize-space(name), $lowercase, $uppercase), ' ')),
                ' -')" />
            <xsl:value-of
                select="substring(substring-before(substring-after($translist,
                    concat('- ', translate(normalize-space(name), $lowercase,
                    $uppercase), ' ')), ' -'), 1, 1)" />
        </xsl:for-each>
    </xsl:with-param>
</xsl:call-template>
</xsl:template>
<!-- /module-startletters -->


<!-- ==================================================================== -->
<!-- define module name translations for sorting                          -->
<!--                                                                      -->
<!-- it's a kind of a hack...                                             -->
<!-- we build a string that contains the following data:                  -->
<!-- "- modulename sortname - modulename sortname - ..."                  -->
<!-- (with all data in uppercase)                                         -->
<!--                                                                      -->
<!-- So, the translation from modulename to sortname can be done with the -->
<!-- expression below:                                                    -->
<!--
       substring-before(
           substring-after($translist, 
                           concat('- ', translate(modulename,
                                                  $lowercase, $uppercase),
                                  ' ')
                           ),
           ' -')
                                                                          -->
<!-- ==================================================================== -->
<xsl:template name="module-translist">
<xsl:param name="modules" />

<xsl:text>-</xsl:text>
<xsl:for-each select="$modules">
    <xsl:variable name="sname" select="translate(normalize-space(name),
                                       $lowercase, $uppercase)" />

    <xsl:text> </xsl:text>
    <xsl:value-of select="$sname" />
    <xsl:text> </xsl:text>
    <xsl:call-template name="module-translatename">
        <xsl:with-param name="name" select="$sname" />
    </xsl:call-template>
    <xsl:text> -</xsl:text>
</xsl:for-each>
</xsl:template>
<!-- /module-translist -->

</xsl:stylesheet>
