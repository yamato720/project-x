ThisBuild / scalaVersion := "2.13.18"
ThisBuild / version := "0.1.0"
ThisBuild / organization := "projectx"

val chiselVersion = "7.11.0"

lazy val root = (project in file("."))
  .settings(
    name := "project-x-chisel",
    scalacOptions ++= Seq(
      "-language:reflectiveCalls",
      "-feature",
      "-Xcheckinit",
      "-Ymacro-annotations"
    ),
    addCompilerPlugin("org.chipsalliance" % "chisel-plugin" % chiselVersion cross CrossVersion.full),
    libraryDependencies ++= Seq(
      "org.chipsalliance" %% "chisel" % chiselVersion
    )
  )
