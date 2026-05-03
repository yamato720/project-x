ThisBuild / scalaVersion := "2.13.10"
ThisBuild / version := "0.1.0"
ThisBuild / organization := "projectx"

lazy val root = (project in file("."))
  .settings(
    name := "project-x-chisel",
    addCompilerPlugin("edu.berkeley.cs" % "chisel3-plugin" % "3.6.0" cross CrossVersion.full),
    libraryDependencies ++= Seq(
      "edu.berkeley.cs" %% "chisel3" % "3.6.0"
    )
  )
